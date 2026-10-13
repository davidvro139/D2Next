// The game session as the client runs it: ticking the world, the game's packets to the client, closing windows, picking
// units under the cursor, file locations.
#include "ClientState.h"
#include "NpcGossip.h"
#include <random>
#include "NpcSpeech.h"
#include "ProgramPath.h"
#include "TouchControls.h"

// The click of the interface's buttons (sounds.txt cursor_button_click).
void buttonSound(){if(gameAudio)gameAudio->effect("cursor\\button.wav");}
// Sprites and the camera snap to whole scene pixels so nearest-neighbour sampling stays stable.

std::filesystem::path executableDirectory() {return d2::programDirectory();}

std::filesystem::path defaultGameDirectory() {
    const auto besideExecutable=executableDirectory()/L"MPQ";
    if(std::filesystem::exists(besideExecutable/L"d2data.mpq"))return besideExecutable;
    const auto workingDirectory=std::filesystem::current_path()/L"MPQ";
    if(std::filesystem::exists(workingDirectory/L"d2data.mpq"))return workingDirectory;
    const std::filesystem::path legacy=L"C:\\games\\Diablo II 1.10";
    if(std::filesystem::exists(legacy/L"d2data.mpq"))return legacy;
    return besideExecutable;
}

std::filesystem::path defaultSaveDirectory() {
    if(const auto privateDirectory=d2::environmentPath("D2NEXT_SAVE_DIR");!privateDirectory.empty())
        return privateDirectory;
    const auto workingDirectory=std::filesystem::current_path();
    if(std::filesystem::exists(workingDirectory/L"MPQ"))return workingDirectory/L"Save";
    return executableDirectory()/L"Save";
}
std::array<float,2> characterAnchor(const d2::MapScene& scene,const CharacterView& view) {
    const auto p=d2::projectPoint(scene,view.x,view.y);
    return {std::round(p[0]),std::round(p[1])};
}
// D2Client 1.10f plays these when a unit is hit (table at 6FB70EFC). The high nibble of dwLastHitClass is the element.
const char* elementOverlayName(uint8_t hitClass) {
    switch(hitClass&0xF0) {
    case 0x20:return "fire_hit";
    case 0x30:return "ice_explode";
    case 0x40:return "lightning";
    case 0x50:return "poisonhit";
    case 0x10:return "doubledamage1";
    case 0x70:return "bash";
    case 0x80:return "hit_thorns";
    case 0x90:return "sanctuaryknockback";
    default:return nullptr;
    }
}
// One 25 Hz simulation step: apply queued commands, then advance the world.
void toggleQuestLog() {
    questLog.open=!questLog.open;
    if(questLog.open) {
        // A left panel: the character sheet and the stash make room. The act shown is the player's.
        characterOpen=false;if(stashOpen||cubeOpen)closeStorage();
        questLog.act=worldView.level>=109?4:worldView.level>=103?3:worldView.level>=75?2:worldView.level>=40?1:0;
        questLog.requestedAt=0;
    }
}
void runTick() {
    // Walking away ends the conversation (menu, speech or store), sending the game the cancel the close button sends (0x30).
    // The original has no distance check of its own: its NPC handlers stop once the NPC leaves the client's unit list
    // (D2Client 0x6FB269F0), and at 800x600 the store and speech cover the screen, so the player can't walk away with them
    // open. Here, with a wider screen and keyboard or stick walking, the dialog closes 10 subtiles (two map cells) away,
    // or when the NPC is no longer in view.
    if(npcDialog.open&&worldView.localPlayer!=d2::EntityId{}) {
        const d2::EntitySnapshot* npc=nullptr;
        for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(npcDialog.guid)){npc=&e;break;}
        const auto& me=worldView.player().position;
        const double dx=npc?double(npc->position.x-me.x)/d2::fixedOne:0,dy=npc?double(npc->position.y-me.y)/d2::fixedOne:0;
        if(!npc||dx*dx+dy*dy>10.0*10.0)closeNpcDialog();
    }
    // A drag released before the picked-up item reached the cursor: dropped now that it has, or forgotten after a second.
    if(itemDrag.waiting) {
        if(cursorItem){itemDrag.waiting=false;uiClick(itemDrag.dropX,itemDrag.dropY,false);}
        else if(nowMilliseconds()>itemDrag.since+1000)itemDrag.waiting=false;
    }
    // The NPC has finished speaking (and the text has scrolled by): back to their menu.
    if(npcDialog.open&&npcDialog.page==NpcDialog::Page::Talk&&nowMilliseconds()>=npcDialog.talkStart+uint64_t((npcDialog.talkSeconds+1)*1000))finishTalk();
    // The open quest log asks the game for the quests (packet 0x40) when it opens and every two seconds.
    if(questLog.open&&(questLog.requestedAt==0||worldView.tick>=questLog.requestedAt+50)) {
        pendingCommands.push_back(d2::Command::raw(0x40|(1<<8),0,0,0,0));questLog.requestedAt=std::max<uint64_t>(worldView.tick,1);
    }
    bool rejected=false,ordered=false;
    for(const auto& command:pendingCommands) {
        const bool ok=(world->apply(worldView.localPlayer,command)==d2::CommandResult::Accepted);
        if(ok&&(command.type==d2::Command::Type::Attack||command.type==d2::Command::Type::CastOn)){actionTargetUnit=command.x;actionTargetIsPoint=false;}
        if(ok&&command.type==d2::Command::Type::CastAt){actionTargetIsPoint=true;actionTargetPoint={command.x,command.y};actionTargetUnit=0;}
        if(ok&&(command.type==d2::Command::Type::CastAt||command.type==d2::Command::Type::CastOn))castSkill=command.z?worldView.rightSkill:worldView.leftSkill;
        if(ok&&command.type==d2::Command::Type::Attack)castSkill=worldView.leftSkill; // a plain attack is the left skill on the unit
        if(command.type==d2::Command::Type::MoveTo){rejected|=!ok;ordered|=ok;}
    }
    pendingCommands.clear();
    world->tick();
    worldView=world->snapshot();
    if(gameLevels) {
        // The weather: outside on levels.txt Rain levels; the act's period of the day colours the rain (D2Common #10933:
        // sunrise and morning dawn, noon day, afternoon and sunset dusk, night).
        const auto* info=gameLevels->byId(int32_t(worldView.level));
        static constexpr std::array<int,6> periods{3,3,0,1,1,2};
        const int act=worldView.level>=109?4:worldView.level>=103?3:worldView.level>=75?2:worldView.level>=40?1:0;
        const auto& p=worldView.player();
        const float x=float(p.position.x)/d2::fixedOne,y=float(p.position.y)/d2::fixedOne;
        gameWeather.update(int32_t(worldView.level),act,info&&info->rain&&!info->inside,periods[size_t(std::min<int>(worldView.envCycle,5))],weatherScreenWidth,weatherScreenHeight,(x-y)*16,(x+y)*8);
    }
    {
        // Newly dropped or newly landed ground items briefly show their labels, like the original client's item-display pulse.
        static std::map<uint32_t,std::tuple<uint32_t,int32_t,int32_t>> itemModes; // id -> mode, x, y
        static uint32_t itemLevel=0;static bool itemModesSeeded=false;
        if(worldView.level!=itemLevel){itemLevel=worldView.level;itemModes.clear();itemLabelUntil.clear();itemModesSeeded=false;}
        std::map<uint32_t,std::tuple<uint32_t,int32_t,int32_t>> now;
        for(const auto& item:worldView.items)if(!item.vendor) {
            now[item.id]={item.mode,item.x,item.y};
            const bool ground=item.mode==3||item.mode==5;
            if(!ground||!itemModesSeeded)continue;
            const auto was=itemModes.find(item.id);
            const bool newlyGround=was==itemModes.end()||!(std::get<0>(was->second)==3||std::get<0>(was->second)==5);
            const bool movedGround=was!=itemModes.end()&&(std::get<1>(was->second)!=item.x||std::get<2>(was->second)!=item.y);
            if(newlyGround||movedGround)itemLabelUntil[item.id]=worldView.tick+100;
        }
        itemModes=std::move(now);itemModesSeeded=true;
        std::erase_if(itemLabelUntil,[](const auto& show){return worldView.tick>=show.second;});
    }
    {
        // Where a missile ended, the effects the client adds itself (its CltHitSubMissiles that stay put: explosions); they
        // last their Range in ticks.
        struct SeenMissile {uint32_t classId;d2::FixedPoint position;int32_t frame;int32_t skill;uint8_t facing;};
        static std::map<d2::EntityId,SeenMissile> before;
        static uint32_t roll=12345;
        auto random=[&](int n){roll=roll*1103515245u+12345u;return int((roll>>16)%uint32_t(std::max(n,1)));};
        // overlay.txt 1ofN: the client rolls, and plays the overlay on a 1-in-N chance (1 always).
        auto playOnce=[&](uint32_t unit,const std::string& name){
            if(name.empty()||name=="null"||!gameOverlays)return;
            if(const auto* info=gameOverlays->find(name);info&&info->oneOfN>1&&random(info->oneOfN)!=0)return;
            unitOverlays.push_back({unit,name,worldView.tick});
            if(std::getenv("D2MODERN_OVERLAY_LOG"))std::cout<<"Overlay "<<name<<" on #"<<unit<<" tick "<<worldView.tick<<"\n";
        };
        std::map<d2::EntityId,SeenMissile> now;
        for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Missile)now[e.id]={e.classId,e.position,e.life,e.maxLife,e.facing};
        // A monster's cast overlay (skills.txt castoverlay). MonsterMsg sends it on the mode update that finds a used skill
        // (AITACTICS_UseSkill stores one, including a melee cast that launches nothing): 0x4C at a target, or 0x4D at a
        // point. SCmd's a9 selects 0x99/0x9A for the same layouts. Byte 1 is the caster's type (1: monster), bytes 2..5 its
        // id, and the skill is a uint16 at byte 6 (0x4C/0x99) or a uint32 there (0x4D/0x9A). The player's own cast overlay
        // is started from the cast mode, so a player caster is not played again here.
        if(gameSkills) {
            const auto& stream=worldView.server;
            auto u32=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
            for(size_t at=0;at+2<=stream.size();) {
                const size_t length=size_t(stream[at])|(size_t(stream[at+1])<<8);at+=2;
                if(at+length>stream.size())break;
                const uint8_t* p=&stream[at];
                const bool targeted=(p[0]==0x4C||p[0]==0x99)&&length>=0x10;
                const bool pointed=(p[0]==0x4D||p[0]==0x9A)&&length>=0x11;
                if((targeted||pointed)&&p[1]==1) {
                    const uint32_t caster=u32(p+2),skill=targeted?uint32_t(p[6]|(p[7]<<8)):u32(p+6);
                    if(caster&&caster!=0xFFFFFFFFu)if(const auto* info=gameSkills->find(skill))playOnce(caster,info->castOverlay);
                }
                at+=length;
            }
        }
        if(std::getenv("D2MODERN_MISSILE_LOG"))for(const auto& [id,m]:now) {
            // A unit with the same server number (missiles and monsters are numbered separately, see missileIdBit).
            const d2::EntitySnapshot* namesake=nullptr;
            for(const auto& e:worldView.entities)if(e.kind!=d2::EntityKind::Missile&&e.id==(id&~d2::EntityId(d2::missileIdBit)))namesake=&e;
            std::cout<<"Missile "<<id<<" class "<<m.classId<<" frame "<<m.frame<<" at "<<double(m.position.x)/d2::fixedOne<<","<<double(m.position.y)/d2::fixedOne<<" tick "<<worldView.tick
                <<(namesake?" (same number as unit class "+std::to_string(namesake->classId)+")":std::string())<<"\n";
        }
        if(gameMissiles&&worldView.tick%3==0)for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId)) {
            // pCltDoFunc 9 spawns its rock and tail once, 10/13 spawn blizzard ice, 3/4 lay poison clouds, 8/18 lay a lightning trail or a bone-spear tail, 6/47 lay one firewall picture per tile, and 48 lays eruption smoke, all below.
            if(info->clientDo==9||info->clientDo==10||info->clientDo==13||info->clientDo==3||info->clientDo==4||info->clientDo==8||info->clientDo==18||info->clientDo==6||info->clientDo==47||info->clientDo==48||std::all_of(info->clientSub.begin(),info->clientSub.end(),[](int row){return row<0;}))continue;
            const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
            float dx=0,dy=0;
            if(const auto previous=before.find(id);previous!=before.end()) {
                dx=float(m.position.x-previous->second.position.x)/d2::fixedOne;
                dy=float(m.position.y-previous->second.position.y)/d2::fixedOne;
            }
            const bool areaEmitter=info->velocity==0&&info->celFile=="null";
            const float base=dx*dx+dy*dy>.0001f?std::atan2(dy,dx):float(random(16))*3.14159265f/8.f;
            for(const int row:info->clientSub) {
                if(row<0)continue;
                const auto* effect=gameMissiles->find(uint32_t(row));if(!effect)continue;
                const float ex=x+(areaEmitter?float(random(121)-60)/10.f:0.f),ey=y+(areaEmitter?float(random(121)-60)/10.f:0.f);
                const uint32_t offset=effect->randStart?uint32_t(random(std::max(1,effect->effectTicks()))):0;
                if(effect->velocity<=0){clientEffects.push_back({uint32_t(row),ex,ey,worldView.tick,0,0,offset});continue;}
                const int count=info->velocity==0?3:2;
                const float speed=float(effect->velocity)*.0516f;
                for(int k=0;k<count;++k) {
                    const float a=base+(float(k)-float(count-1)*.5f)*3.14159265f/3.f;
                    clientEffects.push_back({uint32_t(row),ex,ey,worldView.tick,speed*std::cos(a),speed*std::sin(a),offset});
                }
            }
        }
        // meteorcenter (pCltDoFunc 9): the server missile is the marker on the ground. CltSubMissile1 and 2 (the rock and
        // its tail) are created once and fall onto it. CltParam1 frames, CltParam2 pixels of height and CltParam3 pixels
        // of sideways offset per frame; the game's minimum for each is 1. They are not sprayed again every few ticks.
        if(gameMissiles) {
            static std::set<d2::EntityId> fallen;
            std::set<d2::EntityId> falling;
            for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId);info&&info->clientDo==9) {
                falling.insert(id);
                if(fallen.count(id))continue;
                const int frames=std::max(info->clientParam[0],1),fall=std::max(info->clientParam[1],1),slide=std::max(info->clientParam[2],1);
                const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
                for(int i=0;i<2;++i)if(info->clientSub[size_t(i)]>=0)
                    clientEffects.push_back({uint32_t(info->clientSub[size_t(i)]),x,y,worldView.tick,0,0,0,frames,fall,slide});
            }
            fallen=std::move(falling);
        }
        // Blizzard (pCltDoFunc 13) and a monster blizzard (10). MISSMODE_SrvDo10 spawns the damaging shard on a square of
        // half-width calc1-1, every calc2 frames (skills.txt par1 and par3: 7 and 4). The client spawns one of its two
        // pictures on that same clock (MISSMODE_CreateMissileWithCollisionCheck). Each picture falls from its own
        // CltParam1 pixels at CltParam2 pixels a frame (120 and 5). A server shard is the same fall, kept after the server
        // missile's Range ends: on 1.13c, changing Range did not change the fall. pCltHitFunc 19 then picks one of the two
        // impact missiles. Monster storms use the missile's CltParam the way SrvDo08 does; the missile level is not
        // published, so the level term stays 0.
        if(gameMissiles) {
            auto paramOf=[&](const d2::SkillInfo& skill,const std::string& calc){
                if(calc.size()==4&&calc[0]=='p'&&calc[1]=='a'&&calc[2]=='r'&&calc[3]>='1'&&calc[3]<='8')return skill.param[size_t(calc[3]-'1')];
                return 0;
            };
            auto fallOf=[&](int row,float x,float y){
                const auto* sub=gameMissiles->find(uint32_t(row));if(!sub)return;
                const int frames=sub->straightFall()?sub->clientParam[0]/sub->clientParam[1]:0;
                if(frames>0)clientEffects.push_back({uint32_t(row),x,y,worldView.tick,0,0,0,frames,sub->clientParam[1],0});
                else clientEffects.push_back({uint32_t(row),x,y,worldView.tick,0,0,0});
            };
            static std::set<d2::EntityId> shardSeen;
            std::set<d2::EntityId> shardNow;
            for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId)) {
                const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
                if(info->straightFall()) {
                    shardNow.insert(id);
                    if(!shardSeen.count(id))fallOf(int(m.classId),x,y);
                }
                if(info->clientDo!=10&&info->clientDo!=13)continue;
                int radius=0,every=0;
                if(info->clientDo==13) {
                    const auto* skill=gameSkills?gameSkills->find(uint32_t(m.skill)):nullptr;if(!skill)continue;
                    radius=paramOf(*skill,skill->calc1);every=paramOf(*skill,skill->calc2);
                } else {
                    // SrvDo08 divides the missile level by CltParam3. That level is not published, so the term stays 0.
                    radius=2+info->clientParam[0];every=std::max(info->clientParam[1],3);
                }
                if(radius<=1||every<=0||m.frame%every)continue;
                int choices[2],n=0;for(int i=0;i<2;++i)if(info->clientSub[size_t(i)]>=0)choices[n++]=info->clientSub[size_t(i)];
                if(!n)continue;
                const int span=radius-1;
                const float sx=x+float(random(2*span)-span),sy=y+float(random(2*span)-span);
                if(walkGrid&&!walkGrid->walkable({int(std::lround(sx)),int(std::lround(sy))}))continue;
                fallOf(choices[n>1?random(n):0],sx,sy);
            }
            shardSeen=std::move(shardNow);
        }
        // Poison cloud (pCltDoFunc 4): every CltParam1 frames, CltParam2 copies of CltSubMissile1, each at a random spot
        // inside CltParam3 (d2runewizard). The guide does not say disc or square; the spot is a square of that half-width.
        // SrvDo03 only sets a collision mask, so the puffs are client-only. The puff's own Vel is not applied: the function
        // places it. Poison javelin (pCltDoFunc 3) creates CltSubMissile1 each frame the missile's position changes. CltCalc1
        // would be that cloud's loop count; the missile level is not published, so the cloud keeps its Range. SrvDo02 already
        // creates the same SubMissile1 when the tile changes, and the host publishes it, so that trail is left to the server.
        auto placeStill=[&](int row,float x,float y){
            const auto* sub=gameMissiles?gameMissiles->find(uint32_t(row)):nullptr;if(!sub)return;
            const uint32_t offset=sub->randStart?uint32_t(random(std::max(1,sub->effectTicks()))):0;
            clientEffects.push_back({uint32_t(row),x,y,worldView.tick,0,0,offset});
        };
        auto poisonPuffs=[&](const d2::MissileInfo& info,float x,float y,int age){
            const int rate=info.clientParam[0],count=info.clientParam[1],radius=info.clientParam[2],row=info.clientSub[0];
            if(info.clientDo!=4||rate<=0||count<=0||row<0||age%rate)return;
            const int span=std::max(radius,0);
            for(int i=0;i<count;++i)placeStill(row,x+float(span?random(span*2)-span:0),y+float(span?random(span*2)-span:0));
        };
        if(gameMissiles) {
            static std::map<d2::EntityId,int> cloudAge;
            std::map<d2::EntityId,int> cloudNow;
            for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId)) {
                const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
                if(info->clientDo==4) {
                    const int age=cloudAge.count(id)?cloudAge[id]+1:0;
                    cloudNow[id]=age;poisonPuffs(*info,x,y,age);
                }
                if(info->clientDo!=3||info->clientSub[0]<0)continue;
                if(info->serverDo==2&&info->serverSub==info->clientSub[0])continue;
                bool moving=info->velocity>0;
                if(const auto previous=before.find(id);previous!=before.end())
                    moving=previous->second.position.x!=m.position.x||previous->second.position.y!=m.position.y;
                if(moving)placeStill(info->clientSub[0],x,y);
            }
            cloudAge=std::move(cloudNow);
        }
        // Lightning (pCltDoFunc 8) and Bone Spear (18). Both run every frame; InitSteps is how long the missile has existed.
        // Mishy23's 1.10 tests: 8 is a trail, and the submissile's Range is the length of each graphic (lightningbolt's own
        // cel is null). 18 is one second missile behind the first, a two-part projectile, not a new piece every frame.
        // CltParam1 is that sub's loop count, at least 1. MISSILES_CreateMissileFromParams adds loops*(SubStop-SubStart)
        // frames only when the sub has SubLoop. The guide's "follows the path" is the parent's direction and speed.
        if(gameMissiles) {
            auto lay=[&](int row,float x,float y,float vx,float vy,int facing,int life){
                const auto* sub=gameMissiles->find(uint32_t(row));if(!sub)return;
                const uint32_t offset=sub->randStart?uint32_t(random(std::max(1,sub->effectTicks()))):0;
                clientEffects.push_back({uint32_t(row),x,y,worldView.tick,vx,vy,offset,0,0,0,facing,life});
            };
            static std::set<d2::EntityId> trailed;
            std::set<d2::EntityId> trailNow;
            for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId)) {
                if((info->clientDo!=8&&info->clientDo!=18)||info->clientSub[0]<0)continue;
                const int age=std::max(0,info->range-m.frame);
                if(age<info->initSteps)continue;
                const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
                if(info->clientDo==8) {
                    const auto* sub=gameMissiles->find(uint32_t(info->clientSub[0]));if(!sub)continue;
                    const int loops=std::max(info->clientParam[0],1);
                    const int extra=sub->subLoop&&sub->subStop>sub->subStart?loops*(sub->subStop-sub->subStart):0;
                    lay(info->clientSub[0],x,y,0,0,m.facing,sub->range+extra);
                    continue;
                }
                if(trailed.count(id)){trailNow.insert(id);continue;}
                trailNow.insert(id);
                float vx=0,vy=0;bool stepped=false;
                if(const auto previous=before.find(id);previous!=before.end()) {
                    vx=float(m.position.x-previous->second.position.x)/d2::fixedOne;
                    vy=float(m.position.y-previous->second.position.y)/d2::fixedOne;
                    stepped=vx*vx+vy*vy>0.f;
                }
                if(!stepped) {
                    const float speed=float(info->velocity)*.0516f,a=(float(m.facing)*22.5f+45.f)*3.14159265f/180.f;
                    vx=speed*std::cos(a);vy=speed*std::sin(a);
                }
                lay(info->clientSub[0],x,y,vx,vy,-1,0);
            }
            trailed=std::move(trailNow);
        }
        // Firewall maker (pCltDoFunc 6) and a molten boulder (47, which runs that same function). The guide picks 1 of the
        // 3 client subs. SrvDo06 lays the damaging patch only when the subtile changes (D2Common_10234), so the picture
        // uses that step. CltParam1 is the chance the patch has no light; the roll itself is not given, so the light stays.
        if(gameMissiles)for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId);info&&(info->clientDo==6||info->clientDo==47)) {
            const auto previous=before.find(id);if(previous==before.end())continue;
            if((m.position.x>>16)==(previous->second.position.x>>16)&&(m.position.y>>16)==(previous->second.position.y>>16))continue;
            int choices[3],n=0;for(int i=0;i<3;++i)if(info->clientSub[size_t(i)]>=0)choices[n++]=info->clientSub[size_t(i)];
            if(n)placeStill(choices[n>1?random(n):0],float(m.position.x)/d2::fixedOne,float(m.position.y)/d2::fixedOne);
        }
        // Eruption (pCltDoFunc 48). SrvDo25 lays the crack (SubMissile1) through CreateMissileWithCollisionCheck, radius
        // skill calc1 and frequency calc2 (Eruption par1 7, par2 6), so that picture stays the published missile. The
        // client also lays CltSubMissile2, the smoke, on that clock. The guide sets the crack to dead mode; a second
        // crack here would stack on the published one. The square is that helper's roll (half-width radius-1). Its
        // collision mask is 0x45; walkable is the same stand-in the blizzard path uses.
        if(gameMissiles&&gameSkills)for(const auto& [id,m]:now)if(const auto* info=gameMissiles->find(m.classId);info&&info->clientDo==48&&info->clientSub[1]>=0) {
            const auto* skill=gameSkills->find(uint32_t(m.skill));if(!skill)continue;
            auto paramOf=[&](const std::string& calc){
                if(calc.size()==4&&calc[0]=='p'&&calc[1]=='a'&&calc[2]=='r'&&calc[3]>='1'&&calc[3]<='8')return skill->param[size_t(calc[3]-'1')];
                return 0;
            };
            const int radius=paramOf(skill->calc1),every=paramOf(skill->calc2);
            if(radius<=1||every<=0||m.frame%every)continue;
            const int span=radius-1;
            const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
            const float sx=x+float(random(2*span)-span),sy=y+float(random(2*span)-span);
            if(walkGrid&&!walkGrid->walkable({int(std::lround(sx)),int(std::lround(sy))}))continue;
            placeStill(info->clientSub[1],sx,sy);
        }
        if(gameMissiles)for(const auto& [id,m]:before)if(!now.count(id))if(const auto* info=gameMissiles->find(m.classId)) {
            // The ice burst is played when the falling picture lands, not when this server shard's range ends.
            if(info->straightFall())continue;
            // A missile the server removes at frame 0 or 1 has run out of range. One removed sooner hit something (or a
            // wall). ExplosionMissile and CltHitSubMissile are the pictures the client adds for that hit; AlwaysExplode
            // plays them at the end of the range as well (immolation arrow). Far from the player it only left the
            // published area, which is not a hit.
            const auto& player=worldView.player().position;
            const int64_t dx=int64_t(m.position.x)-player.x,dy=int64_t(m.position.y)-player.y;
            if(dx*dx+dy*dy>int64_t(96)*96*65536*65536||(m.frame<=1&&!info->alwaysExplode))continue;
            auto spawnHit=[&](int hit,bool skipServerDuplicate) {
                if(hit<0)return;
                const auto* effect=gameMissiles->find(uint32_t(hit));if(!effect)return;
                const float x=float(m.position.x)/d2::fixedOne,y=float(m.position.y)/d2::fixedOne;
                const uint32_t offset=effect->randStart?uint32_t(random(std::max(1,effect->effectTicks()))):0;
                if(effect->velocity==0){clientEffects.push_back({uint32_t(hit),x,y,worldView.tick,0,0,offset});return;}
                // A moving one the server also makes (Fist of the Heavens' holy bolts, only at undead in range) arrives as its
                // own missiles, aimed as the game aims them: nothing to add.
                if(skipServerDuplicate&&std::find(info->serverHit.begin(),info->serverHit.end(),hit)!=info->serverHit.end())return;
                // Other moving ones: a ring of 8 flying out, at missiles.txt Vel (about 0.0516 subtiles a tick per unit,
                // measured on the server's missiles).
                const float speed=float(effect->velocity)*.0516f;
                for(int k=0;k<8;++k){const float a=float(k)*3.14159265f/4;clientEffects.push_back({uint32_t(hit),x,y,worldView.tick,speed*std::cos(a),speed*std::sin(a),offset});}
            };
            spawnHit(info->explosion,false);
            // pCltHitFunc pictures that are not the generic in-place or ring-of-8. The guide names the count a density for
            // meteor and does not give coordinates; the spots are the 18 offsets CreateMeteor_MoltenBoulderSubmissiles uses,
            // taken evenly. A molten boulder's server hit lays the same fire on a large monster, so those can stack.
            const float hx=float(m.position.x)/d2::fixedOne,hy=float(m.position.y)/d2::fixedOne;
            auto drop=[&](int row,float px,float py,float vx,float vy,int facing,int life,int rate){
                if(row<0)return;
                const auto* effect=gameMissiles->find(uint32_t(row));if(!effect)return;
                const uint32_t offset=effect->randStart?uint32_t(random(std::max(1,effect->effectTicks()))):0;
                clientEffects.push_back({uint32_t(row),px,py,worldView.tick,vx,vy,offset,0,0,0,facing,life,rate});
            };
            static const int ox[18]={2,-2,0,0,-3,0,3,-1,1,-1,2,-4,-3,-1,0,1,3,4};
            static const int oy[18]={-2,-2,2,5,3,3,3,2,1,-1,-1,-2,-2,-3,-4,-3,-3,-2};
            auto ring=[&](int row,int count){
                if(row<0||count<=0)return;
                const int n=std::min(count,18);
                for(int i=0;i<n;++i){const int s=i*18/n;drop(row,hx+float(ox[s]),hy+float(oy[s]),0,0,-1,0,0);}
            };
            bool shaped=true;
            switch(info->clientHitFunc) {
            case 1: { // disc: cHitPar1 radius, cHitPar2 count
                const int radius=info->clientHitParam[0],count=info->clientHitParam[1];
                if(count>0)for(int i=0;i<count;++i){const float a=float(i)*6.2831853f/float(count);drop(info->clientHit[0],hx+float(radius)*std::cos(a),hy+float(radius)*std::sin(a),0,0,-1,0,0);}
                break;
            }
            case 3: { // one of the two debris missiles, after the first sub
                drop(info->clientHit[0],hx,hy,0,0,-1,0,0);
                int choice[2],n=0;if(info->clientHit[1]>=0)choice[n++]=info->clientHit[1];if(info->clientHit[2]>=0)choice[n++]=info->clientHit[2];
                if(n)drop(choice[n>1?random(n):0],hx,hy,0,0,-1,0,0);
                break;
            }
            case 14: // the center as the missile was facing, the ejecta on a random rotation
                drop(info->clientHit[0],hx,hy,0,0,m.facing,0,0);drop(info->clientHit[1],hx,hy,0,0,random(16),0,0);break;
            case 18: { // rings of cHitPar1, cHitPar2, cHitPar3, and one light whose range is the skill's Param3
                ring(info->clientHit[0],info->clientHitParam[0]);
                int life=0;if(m.skill>0&&gameSkills)if(const auto* skill=gameSkills->find(uint32_t(m.skill)))life=skill->param[2];
                drop(info->clientHit[1],hx,hy,0,0,-1,life,0);
                ring(info->clientHit[2],info->clientHitParam[1]);ring(info->clientHit[3],info->clientHitParam[2]);
                break;
            }
            case 24: case 29: case 44: case 54: // one sub where it hit, facing the way the missile did
                drop(info->clientHit[0],hx,hy,0,0,m.facing,0,0);break;
            case 31: { // the melt, with its animation rate forced to 1. Medium uses the large melt.
                const char* melt=m.classId==272?"icebreaksmallmelt":(m.classId==273||m.classId==274||m.classId==417)?"icebreaklargemelt":nullptr;
                if(melt)drop(gameMissiles->rowOf(melt),hx,hy,0,0,-1,0,1);
                break;
            }
            case 39: drop(int(m.classId)+1+random(2),hx,hy,0,0,-1,0,0);break; // one of the next two missile rows
            case 41: { // three debris inside radius 6, and the smoke where the rock was
                if(const int debris=gameMissiles->rowOf("durieldeathdebris");debris>=0) {
                    const auto* sub=gameMissiles->find(uint32_t(debris));const float speed=sub?float(sub->velocity)*.0516f:0;
                    for(int i=0;i<3;++i){const float a=float(random(256))*6.2831853f/256.f,d=float(random(7));drop(debris,hx+d*std::cos(a),hy+d*std::sin(a),speed*std::cos(a),speed*std::sin(a),-1,0,0);}
                }
                drop(gameMissiles->rowOf("durieldeathsmoke"),hx,hy,0,0,-1,0,0);
                break;
            }
            case 52: { // the burst, cHitPar1 rocks aimed out to cHitPar1*2 at velocity cHitPar2, then 18 fires
                drop(info->clientHit[0],hx,hy,0,0,-1,0,0);
                const int count=info->clientHitParam[0];const float speed=float(info->clientHitParam[1])*.0516f;
                if(count>0)for(int i=0;i<count;++i){const float a=float(random(256))*6.2831853f/256.f;drop(info->clientHit[1],hx,hy,speed*std::cos(a),speed*std::sin(a),-1,0,0);}
                ring(info->clientHit[2],18);
                break;
            }
            default: shaped=false;break;
            }
            if(!shaped)for(const int hit:info->clientHit)spawnHit(hit,true);
        }
        before=std::move(now);
        // Effects the client adds for units' states (states.txt cltactivefunc and the redeemed state's skill): Sanctuary's
        // sparkles (sanctuarybolt, Gleam) around the paladin, Open Wounds' blood dripping, and Redemption's ghost where a
        // corpse is redeemed. The game's own client functions pick the spots; these are spread at random nearby.
        static std::map<d2::EntityId,bool> redeemed;
        auto has=[](const std::array<uint32_t,6>& states,int s){return (states[size_t(s>>5)]&(1u<<(s&31)))!=0;};
        auto spawn=[&](const std::string& name,float x,float y,float vx=0,float vy=0){
            if(const int row=gameMissiles?gameMissiles->rowOf(name):-1;row>=0) {
                const auto* info=gameMissiles->find(uint32_t(row));
                const uint32_t offset=info&&info->randStart?uint32_t(random(std::max(1,info->effectTicks()))):0;
                clientEffects.push_back({uint32_t(row),x,y,worldView.tick,vx,vy,offset});
            }
        };
        std::map<d2::EntityId,bool> redeemedNow;
        // A monster's hit by an element: when it falls into death, get-hit or knockback (modes 0, 3, 13) the client plays
        // the element's overlay over it. D2Game's hit class high nibble carries the element even for pure spell hits such as
        // Fire Bolt (0x20) and Charged Bolt (0x40), where the low weapon-kind nibble is zero.
        static std::map<d2::EntityId,uint8_t> modes;
        std::map<d2::EntityId,uint8_t> modesNow;
        for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy||e.kind==d2::EntityKind::Corpse) {
            modesNow[e.id]=e.mode;
            const auto was=modes.find(e.id);
            if(was==modes.end()||was->second==e.mode||(e.mode!=0&&e.mode!=3&&e.mode!=13))continue;
            if(const char* name=elementOverlayName(e.hitClass))playOnce(uint32_t(e.id-1000),name);
        }
        modes=std::move(modesNow);
        // The player's own hit. The reply has no hit class; PlrMsg sends it. Packet 0x0D's byte 6 is the mode graphic
        // (SCmd stores it in nHitClass) and byte 0x0B is dwLastHitClass (nHitType): 0x06 get-hit, 0x08 death. Packet 0x0F
        // graphic 0x14 is knockback (sub_6FC81F60), hit class again at byte 0x0B. Block is 0x12 and the resting dead mode
        // is 0x09; both carry a hit class and do not play this. Drawn on the player (unit 0).
        {
            const auto& stream=worldView.server;
            auto u32=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
            for(size_t at=0;at+2<=stream.size();) {
                const size_t length=size_t(stream[at])|(size_t(stream[at+1])<<8);at+=2;
                if(at+length>stream.size())break;
                const uint8_t* p=&stream[at];
                if(length>=0x0D&&p[1]==0&&u32(p+2)==worldView.nativeGuid) {
                    if(p[0]==0x0D&&(p[6]==0x06||p[6]==0x08)){if(const char* name=elementOverlayName(p[0x0B]))playOnce(0,name);}
                    else if(p[0]==0x0F&&length>=0x10&&p[6]==0x14){if(const char* name=elementOverlayName(p[0x0B]))playOnce(0,name);}
                }
                at+=length;
            }
        }
        for(const auto& e:worldView.entities) {
            const float x=float(e.position.x)/d2::fixedOne,y=float(e.position.y)/d2::fixedOne;
            if(has(e.states,47)&&worldView.tick%3==0)spawn("sanctuarybolt",x+float(random(81)-40)/10,y+float(random(81)-40)/10);
            if(has(e.states,62)&&worldView.tick%8==0){const float a=float(random(16))*3.14159265f/8;spawn("blood1",x,y,.05f*std::cos(a),.05f*std::sin(a));}
            if(e.kind==d2::EntityKind::Missile)continue;
            const bool on=has(e.states,99);redeemedNow[e.id]=on;
            if(on&&!redeemed[e.id]) {
                spawn("redemption",x,y);
                if(std::getenv("D2MODERN_MISSILE_LOG"))std::cout<<"Redeemed "<<e.id<<" tick "<<worldView.tick<<"\n";
            }
        }
        redeemed=std::move(redeemedNow);
        // states.txt castoverlay / removerlay: played once when a state starts or ends (a curse's flash, poison's hit).
        // The first report only records what is already on, so loading in does not replay them.
        {
            static std::array<uint32_t,6> playerStates{};
            static std::map<d2::EntityId,std::array<uint32_t,6>> unitStates;
            static bool seeded=false;
            auto edges=[&](uint32_t unit,const std::array<uint32_t,6>& states,const std::array<uint32_t,6>* was) {
                if(!was||!gameStates)return;
                for(uint32_t s=0;s<192;++s) {
                    const bool on=(states[s>>5]&(1u<<(s&31)))!=0,prev=((*was)[s>>5]&(1u<<(s&31)))!=0;
                    if(on==prev)continue;
                    const auto* info=gameStates->find(s);if(!info)continue;
                    playOnce(unit,on?info->castOverlay:info->removeOverlay);
                }
            };
            if(!seeded) {
                playerStates=worldView.player().states;
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy||e.kind==d2::EntityKind::Corpse||e.kind==d2::EntityKind::TownPerson)unitStates[e.id]=e.states;
                seeded=true;
            } else {
                edges(0,worldView.player().states,&playerStates);playerStates=worldView.player().states;
                std::map<d2::EntityId,std::array<uint32_t,6>> next;
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy||e.kind==d2::EntityKind::Corpse||e.kind==d2::EntityKind::TownPerson) {
                    const auto it=unitStates.find(e.id);
                    edges(uint32_t(e.id-1000),e.states,it==unitStates.end()?nullptr:&it->second);
                    next[e.id]=e.states;
                }
                unitStates=std::move(next);
            }
        }
        std::erase_if(unitOverlays,[](const UnitOverlay& o){const auto* info=gameOverlays?gameOverlays->find(o.name):nullptr;return !info||(worldView.tick-o.start)*uint64_t(info->rate)/16>=info->frames;});
        // A straight fall shows its landing frame, then pCltHitFunc 19 picks one of the first two client hit missiles.
        {
            std::vector<ClientEffect> landed;
            for(const auto& e:clientEffects)if(e.fallFrames>0&&e.slideRate==0&&worldView.tick>=e.born+uint64_t(e.fallFrames+1))landed.push_back(e);
            for(const auto& e:landed)if(const auto* info=gameMissiles?gameMissiles->find(e.classId):nullptr) {
                int hits[2],n=0;for(int i=0;i<2;++i)if(info->clientHit[size_t(i)]>=0)hits[n++]=info->clientHit[size_t(i)];
                if(n)clientEffects.push_back({uint32_t(hits[n>1?random(n):0]),e.x,e.y,worldView.tick,0,0,0});
                if(gameAudio&&gameSounds) {
                    const auto& p=worldView.player().position;
                    const double dx=e.x-double(p.x)/d2::fixedOne,dy=e.y-double(p.y)/d2::fixedOne;
                    if(dx*dx+dy*dy<30.0*30.0)if(const auto file=gameSounds->file(info->hitSound);!file.empty())gameAudio->effect(file);
                }
            }
        }
        // A cloud this client laid (plague javelin's trail, a hit puff) runs the same puff clock. Age 0 is its first frame.
        if(gameMissiles) {
            const size_t laid=clientEffects.size();
            for(size_t i=0;i<laid;++i) {
                const ClientEffect e=clientEffects[i];
                const auto* info=gameMissiles->find(e.classId);if(!info)continue;
                const float age=float(worldView.tick-e.born);
                poisonPuffs(*info,e.x+e.vx*age,e.y+e.vy*age,int(worldView.tick-e.born));
            }
        }
        std::erase_if(clientEffects,[](const ClientEffect& e){const auto* info=gameMissiles?gameMissiles->find(e.classId):nullptr;if(!info)return true;const uint64_t life=e.life>0?uint64_t(e.life):e.fallFrames>0&&e.slideRate==0?uint64_t(e.fallFrames+1):uint64_t(info->effectTicks());return worldView.tick>=e.born+life;});
    }
    {
        // Attacking, casting, throwing, kicking or a special move: the player faces the target (0 is screen-down, 16 steps
        // clockwise), and keeps facing that way until walking again.
        const auto& p=worldView.player();
        // A swing that follows another keeps the mode; its animation starting over (the frame going back) marks it instead.
        static uint32_t lastFrame=0;
        const bool restarted=p.mode==lastPlayerMode&&p.animationFrame!=UINT32_MAX&&p.animationFrame<lastFrame;
        lastFrame=p.animationFrame==UINT32_MAX?0:p.animationFrame;
        // The cast overlay restarts with each cast, including one that never leaves the cast mode.
        if(p.mode==10&&(lastPlayerMode!=10||restarted))castStart=worldView.tick;
        const bool actionMode=(p.mode>=7&&p.mode<=8)||p.mode==10||(p.mode>=13&&p.mode<=16)||p.mode==18;
        struct CorpseTarget {int x,y;uint32_t classId;};
        static std::optional<CorpseTarget> corpseTarget;
        auto targetPosition=[&]() -> std::optional<std::array<double,2>> {
            if(actionTargetIsPoint)return std::array<double,2>{actionTargetPoint.x+.5,actionTargetPoint.y+.5};
            if(actionTargetUnit)for(const auto& e:worldView.entities)if(e.id==d2::EntityId(1000+actionTargetUnit))return std::array<double,2>{double(e.position.x)/d2::fixedOne,double(e.position.y)/d2::fixedOne};
            return std::nullopt;
        };
        if((p.mode!=lastPlayerMode||restarted)&&actionMode&&gameMissiles&&gameSkills) {
            // An attack or cast begins: a skill's own slow client missile (skills.txt cltmissilea, Vel 3 or less: Sacrifice's
            // blood) sprays from the player, three drops. Faster ones are the server's missiles too.
            if(std::getenv("D2MODERN_MISSILE_LOG"))std::cout<<"Swing: mode "<<int(p.mode)<<", skill "<<castSkill<<", tick "<<worldView.tick<<"\n";
            if(const auto* skill=gameSkills->find(uint32_t(castSkill))) {
                // A curse starts (cltstfunc 18, D2Client 0x6FAC7F90) with one cltmissilec (cursecast) where the target is: the
                // path's target unit, else its target point (0x6FABABC0). Its own picture comes on the frame event (below).
                // Curses and corpse explosions add their client missiles from their own functions (below), not at the start.
                const bool ownMissiles=skill->clientStart==18||skill->clientDo==30||skill->clientDo==8||skill->clientStart==19||skill->clientStart==20||skill->clientStart==21||skill->clientStart==24;
                // Teeth and Bone Spear start (cltstfunc 19, 0x6FAC8150) with one cltmissilec (bonecast) 2 subtiles from the caster
                // toward the target (2*d/distance, UNITS_GetDistanceToCoordinates at least 1), facing the target.
                if(skill->clientStart==19)if(const auto target=targetPosition())if(const int row=gameMissiles->rowOf(skill->clientMissiles[2]);row>=0) {
                    const double ux=std::floor(double(p.position.x)/d2::fixedOne),uy=std::floor(double(p.position.y)/d2::fixedOne);
                    const int dx=int(std::floor((*target)[0]))-int(ux),dy=int(std::floor((*target)[1]))-int(uy);
                    const int distance=std::max(1,int(std::lround(std::sqrt(double(dx*dx+dy*dy)))));
                    ClientEffect e{uint32_t(row),float(ux+double(2*dx/distance))+.5f,float(uy+double(2*dy/distance))+.5f,worldView.tick,0,0,0};
                    if(dx||dy)e.facing=((int(std::lround((std::atan2(double(dy),double(dx))*180/3.14159265358979-45)/22.5))%16)+16)%16;
                    clientEffects.push_back(e);
                }
                // Revive starts (cltstfunc 24, 0x6FAC8D00) with one picture on the corpse, by its monstats2 OverlayHeight:
                // 1 cltmissileb (small), 3 cltmissilec (large), otherwise cltmissilea.
                if(skill->clientStart==24&&actionTargetUnit)for(const auto& e:worldView.entities)if(e.id==d2::EntityId(1000+actionTargetUnit)) {
                    const auto* sprite=gameUnitArt?gameUnitArt->sprite(e.classId):nullptr;
                    const int height=sprite?sprite->overlayHeight:2;
                    if(const int row=gameMissiles->rowOf(skill->clientMissiles[height==1?1:height==3?2:0]);row>=0)
                        clientEffects.push_back({uint32_t(row),float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne,worldView.tick,0,0,0});
                }
                // A corpse explosion starts (cltstfunc 21, 0x6FAC8530) by keeping the target corpse's subtile and class in the
                // skill (SKILLS_SetParam1-3); its do function explodes there. Raising a skeleton (20, 0x6FAC8370) keeps its subtile.
                if(skill->clientStart==21||skill->clientStart==20) {
                    corpseTarget.reset();
                    for(const auto& e:worldView.entities)if(e.id==d2::EntityId(1000+actionTargetUnit)&&actionTargetUnit)
                        corpseTarget=CorpseTarget{int(e.position.x/d2::fixedOne),int(e.position.y/d2::fixedOne),e.classId};
                }
                if(skill->clientStart==18)if(const int row=gameMissiles->rowOf(skill->clientMissiles[2]);row>=0) {
                    const auto target=targetPosition();
                    const float x=target?float((*target)[0]):float(p.position.x)/d2::fixedOne,y=target?float((*target)[1]):float(p.position.y)/d2::fixedOne;
                    clientEffects.push_back({uint32_t(row),x,y,worldView.tick,0,0,0});
                }
                std::set<std::string> overlays;
                for(const auto& name:skill->clientOverlays)if(!name.empty())overlays.insert(name);
                const bool chargeUp=skill->clientProgress[0]==12||skill->clientProgress[0]==13; // their charge overlay comes with 0xA3
                if(!skill->progressOverlay.empty()&&!chargeUp)overlays.insert(skill->progressOverlay);
                for(const auto& name:overlays)unitOverlays.push_back({0,name,worldView.tick});
                if(actionTargetUnit&&!skill->targetOverlay.empty())unitOverlays.push_back({uint32_t(actionTargetUnit),skill->targetOverlay,worldView.tick});
                if(actionTargetUnit&&!skill->serverOverlay.empty())unitOverlays.push_back({uint32_t(actionTargetUnit),skill->serverOverlay,worldView.tick});
                std::set<int> spawned;
                std::set<std::string> serverMissiles;
                for(const auto& server:skill->serverMissiles)if(!server.empty())serverMissiles.insert(server);
                if(!ownMissiles)for(const auto& name:skill->clientMissiles)if(!name.empty()) {
                    const int row=gameMissiles->rowOf(name);
                    if(row<0||spawned.count(row))continue;
                    spawned.insert(row);
                    const bool visualOnly=!serverMissiles.count(name);
                    if(const auto* m=gameMissiles->find(uint32_t(row));m&&(m->velocity<=3||visualOnly)) {
                        const auto target=targetPosition();
                        const bool atTarget=visualOnly&&target&&m->velocity<=3;
                        const bool selfVisual=visualOnly&&!target;
                        const float x=atTarget?float((*target)[0]):float(p.position.x)/d2::fixedOne;
                        const float y=atTarget?float((*target)[1]):float(p.position.y)/d2::fixedOne;
                        const float speed=atTarget?0.f:float(std::max(m->velocity,1))*.0516f;
                        const uint32_t offset=m->randStart?uint32_t((worldView.tick*1103515245u+uint64_t(row)*12345u)%uint32_t(std::max(1,m->effectTicks()))):0;
                        const int count=selfVisual&&m->velocity>3?8:visualOnly&&m->velocity>3?1:3;
                        for(int k=0;k<count;++k){
                            float a=visualOnly&&m->velocity>3?float(p.facing)*3.14159265f/8.f:float((worldView.tick*7+uint64_t(k)*5)%16)*3.14159265f/8;
                            if(selfVisual&&m->velocity>3)a=(float(k)/float(count)*2.f+float(worldView.tick%8)/8.f)*3.14159265f;
                            if(visualOnly&&target&&!atTarget)a=std::atan2(float((*target)[1])-y,float((*target)[0])-x);
                            clientEffects.push_back({uint32_t(row),x,y,worldView.tick,speed*std::cos(a),speed*std::sin(a),offset});
                        }
                    }
                }
            }
        }
        // The frame event of an attack or cast, when D2Client runs the skill's cltdofunc. A curse's (30, 0x6FAC7FF0) adds an
        // invisible cursecenter where the target is, whose stream missile is the curse's cltmissilea and whose stream range is
        // aurarangecalc - 3, at least 2. cursecenter's pCltDoFunc 17 (0x6FB32310) fills a disc of that radius (0x6FB35D10) on
        // each frame whose remaining count is below CltParam1 and divisible by 3: every spot `step` subtiles apart within the
        // radius, each with a 1-in-`count` chance (a radius above 4: count radius/2, step 2; otherwise count radius, step 1).
        // The center is gone when its frames reach 0, so the last disc is at 3 remaining.
        // Mind Blast (cltdofunc 8, 0x6FABFD40) does the same with its own visible center: cltmissilea (mindblast center,
        // pCltDoFunc 17 too) carrying cltmissileb (mindblast hit) over a disc of aurarangecalc, without the - 3.
        struct CurseCenter {int center,picture;float x,y;int radius;uint64_t born;};
        if(p.frameEvent&&actionMode&&castSkill)pendingDoSoundSkill=uint32_t(castSkill);
        if(p.frameEvent&&std::getenv("D2MODERN_MISSILE_LOG"))std::cout<<"Frame event: mode "<<int(p.mode)<<", skill "<<castSkill<<", tick "<<worldView.tick<<"\n";
        static std::vector<CurseCenter> curseCenters;
        static uint32_t curseRoll=0x9E3779B9u;
        if(p.frameEvent&&actionMode&&gameMissiles&&gameSkills)if(const auto* skill=gameSkills->find(uint32_t(castSkill));skill&&(skill->clientDo==30||skill->clientDo==8)) {
            int level=0;for(const auto& s:worldView.skills)if(s.id==skill->id)level=s.level;
            const bool curse=skill->clientDo==30;
            const int center=curse?gameMissiles->rowOf("cursecenter"):gameMissiles->rowOf(skill->clientMissiles[0]);
            const int picture=gameMissiles->rowOf(skill->clientMissiles[curse?0:1]);
            const int range=skill->levelCalc(skill->auraRangeCalc,level);
            if(const auto target=targetPosition();target&&picture>=0&&center>=0) {
                curseCenters.push_back({center,picture,float((*target)[0]),float((*target)[1]),curse?std::max(range-3,2):range,worldView.tick});
                if(!curse)clientEffects.push_back({uint32_t(center),float((*target)[0]),float((*target)[1]),worldView.tick,0,0,0});
                if(std::getenv("D2MODERN_MISSILE_LOG"))std::cout<<"Curse center: skill "<<skill->id<<" level "<<level<<", radius "<<curseCenters.back().radius<<", tick "<<worldView.tick<<"\n";
            }
        }
        // Corpse Explosion's frame event (cltdofunc 32, 0x6FAC85F0). Pieces of its cltmissilea (the guts) are laid around the
        // corpse by 0x6FABD8A0: count pieces, each within spread subtiles, facing the four diagonals in turn from a random
        // one. By the corpse's monstats2 size: small 1 piece; large 3 within 3, then 4 of the guts' SubMissile1 within 2;
        // otherwise 2 within 2, then 3 of SubMissile2 within 1. Then its cltmissileb (the burst) on the corpse. Poison
        // Explosion (33, 0x6FAC8940) lays one cltmissilea there; its cloud is the server's. Both end with cltmissilec, a
        // light with no picture.
        if(p.frameEvent&&actionMode&&corpseTarget&&gameMissiles&&gameSkills)if(const auto* skill=gameSkills->find(uint32_t(castSkill));skill&&(skill->clientDo==32||skill->clientDo==33||skill->clientDo==31)) {
            const auto c=*corpseTarget;
            auto roll=[&](uint32_t n){curseRoll=curseRoll*1103515245u+12345u;return n?(curseRoll>>16)%n:0u;};
            auto lay=[&](int row,int spread,int count) {
                if(row<0)return;
                uint32_t quadrant=roll(4);
                for(int i=0;i<count;++i) {
                    quadrant=(quadrant+1)&3;
                    const int dx=spread>0?int(roll(uint32_t(2*spread)))-spread:0,dy=spread>0?int(roll(uint32_t(2*spread)))-spread:0;
                    ClientEffect e{uint32_t(row),float(c.x+dx)+.5f,float(c.y+dy)+.5f,worldView.tick,0,0,0};
                    e.facing=int(quadrant*4); // PATH_SetDirection(quadrant*16) of 64
                    clientEffects.push_back(e);
                }
            };
            const int guts=gameMissiles->rowOf(skill->clientMissiles[0]);
            // Raise Skeleton and Raise Skeletal Mage (31, 0x6FAC8440): one cltmissilea (corpseexplosion) on the corpse, unturned.
            if(skill->clientDo==31){if(guts>=0)clientEffects.push_back({uint32_t(guts),float(c.x)+.5f,float(c.y)+.5f,worldView.tick,0,0,0});}
            else if(skill->clientDo==33)lay(guts,0,1);
            else {
                const auto* sprite=gameUnitArt?gameUnitArt->sprite(c.classId):nullptr;
                const auto* info=guts>=0?gameMissiles->find(uint32_t(guts)):nullptr;
                if(sprite&&sprite->small)lay(guts,0,1);
                else if(sprite&&sprite->large){lay(guts,3,3);if(info)lay(info->serverSub,2,4);}
                else {lay(guts,2,2);if(info)lay(info->serverSub2,1,3);}
                lay(gameMissiles->rowOf(skill->clientMissiles[1]),0,1);
            }
            if(skill->clientDo!=31)lay(gameMissiles->rowOf(skill->clientMissiles[2]),0,1); // cltmissilec: a light with no picture (red, green)
            if(std::getenv("D2MODERN_MISSILE_LOG"))std::cout<<"Corpse explosion: skill "<<skill->id<<", corpse class "<<c.classId<<" at "<<c.x<<","<<c.y<<", tick "<<worldView.tick<<"\n";
            corpseTarget.reset();
        }
        clientLights.clear();
        if(gameMissiles) {
            for(const auto& c:curseCenters) {
                // The curse center's light (0x6FAA4280: intensity 255, red 255, green and blue 0; 0x6FAA43A0 sets its radius to
                // the curse's, at most 18). Mind Blast's center has none.
                if(c.center==gameMissiles->rowOf("cursecenter"))clientLights.push_back({c.x,c.y,float(std::min(c.radius,18)),{1.f,0.f,0.f}});
                const auto* center=gameMissiles->find(uint32_t(c.center));if(!center)continue;
                const int remaining=center->range-int(worldView.tick-c.born);
                if(remaining<=0||remaining>=center->clientParam[0]||remaining%3)continue;
                const int count=c.radius>4?c.radius/2:c.radius,step=c.radius>4?2:1;
                for(int dy=-c.radius;dy<=c.radius;dy+=step)for(int dx=-c.radius;dx<=c.radius;dx+=step) {
                    if(dx*dx+dy*dy>c.radius*c.radius)continue;
                    curseRoll=curseRoll*1103515245u+12345u;
                    if(count>0&&(curseRoll>>16)%uint32_t(count))continue;
                    clientEffects.push_back({uint32_t(c.picture),c.x+float(dx),c.y+float(dy),worldView.tick,0,0,0});
                }
            }
            std::erase_if(curseCenters,[&](const CurseCenter& c){const auto* center=gameMissiles->find(uint32_t(c.center));return !center||worldView.tick<c.born||worldView.tick>=c.born+uint64_t(std::max(center->range,0));});
        }
        lastPlayerMode=p.mode;
        if(p.mode>=7&&p.mode<=16&&p.mode!=9) {
            std::optional<std::array<double,2>> target;
            if(actionTargetIsPoint)target=std::array<double,2>{actionTargetPoint.x+.5,actionTargetPoint.y+.5};
            else if(actionTargetUnit)for(const auto& e:worldView.entities)if(e.id==d2::EntityId(1000+actionTargetUnit))target=std::array<double,2>{double(e.position.x)/d2::fixedOne,double(e.position.y)/d2::fixedOne};
            const double dx=target?(*target)[0]-double(p.position.x)/d2::fixedOne:0,dy=target?(*target)[1]-double(p.position.y)/d2::fixedOne:0;
            if(target&&dx*dx+dy*dy>0.01) {
                const int step=int(std::lround((std::atan2(dy,dx)*180.0/3.14159265358979323846-45.0)/22.5));
                heldFacing=((step%16)+16)%16;
            }
        } else if(p.moving)heldFacing=-1;
    }
    {static uint32_t lastLevel=0;if(worldView.level!=lastLevel){lastLevel=worldView.level;waypointMenu=WaypointMenu{};}} // a new level: the travel already ended the waypoint use, so nothing is sent
    if(afterTick)afterTick();
    for(const auto& o:worldView.objects)objectClasses[o.id]=o.classId;
    handleServerPackets();
    for(const auto& o:worldView.objects){auto& clock=objectClocks[o.id];if(clock.mode!=o.mode){clock.mode=o.mode;clock.ticks=0;}else ++clock.ticks;}
    if(rejected)walkStatus="Blocked or unreachable";
    else if(ordered||worldView.player().moving)walkStatus="Walking";
    else if(walkStatus=="Walking")walkStatus="Ready";
}
int32_t beltItem(int column) {
    const d2::ItemSnapshot* best=nullptr;
    for(const auto& item:worldView.items)if(item.mode==2&&item.x==column&&(!best||item.y<best->y))best=&item;
    return best?int32_t(best->id):0;
}
void closeStorage() {
    // Button 18 closes the stash, 23 the cube (packet 0x4F: button, then the gold amount, none here).
    // The stash came with the inventory and goes with it; the cube was opened from the inventory, which stays.
    if(stashOpen){pendingCommands.push_back(d2::Command::raw(0x4F|(7<<8),18,0,0,0));inventoryOpen=false;}
    if(cubeOpen)pendingCommands.push_back(d2::Command::raw(0x4F|(7<<8),23,0,0,0));
    stashOpen=cubeOpen=false;
}
// Closing the menu without travelling tells the game, as the original does (D2Client 0x6FB26299 and the other close
// paths: packet 0x49 with level 0): the server holds the player busy with the waypoint until then, and refuses the next
// waypoint (or any object) the player uses.
void closeWaypointMenu() {
    if(waypointMenu.open&&waypointMenu.guid)pendingCommands.push_back(d2::Command::raw(0x49|(9<<8),waypointMenu.guid,0,0,0));
    waypointMenu=WaypointMenu{};
}
std::vector<std::string> talkRows() {
    std::vector<std::string> shown;
    if(!gameStrings)return shown;
    // One message at a time, as the game plays them: the first of the NPC's lines.
    for(const auto id:npcDialog.speaking?std::vector<uint16_t>{npcDialog.speaking}:std::vector<uint16_t>{}) {
        const std::string text=gameStrings->byId(id);std::vector<std::string> rows;size_t start=0;
        while(start<=text.size()){size_t end=text.find('\n',start);if(end==std::string::npos)end=text.size();auto row=text.substr(start,end-start);while(!row.empty()&&row.back()=='\r')row.pop_back();rows.push_back(row);start=end+1;}
        // The first row of a spoken line is a number (not shown).
        for(size_t r=(rows.size()>1&&!rows[0].empty()&&std::all_of(rows[0].begin(),rows[0].end(),[](char c){return std::isdigit(static_cast<unsigned char>(c));}))?1:0;r<rows.size();++r)shown.push_back(rows[r]);
        shown.push_back("");
    }
    while(!shown.empty()&&shown.back().empty())shown.pop_back();
    return shown;
}
// D2Client 0x6FAF44C0, once a game for every NPC of its table: up to 10 random rows past the two introductions, the first
// for the player's class (7 any) whose quest condition holds (the quest's state bit 0, QUESTRECORD_GetQuestState) is the
// NPC's gossip; row 2 when none is found. The original draws from the player's seed; here from the game's own random.
static void chooseGossip() {
    static std::mt19937 random{std::random_device{}()};
    gossipChoice.clear();
    for(const auto& npc:d2::npcGossip()) {
        if(gossipChoice.count(npc.monsterClass))continue;
        int chosen=2;
        for(int tries=0;tries<10&&!npc.lines.empty();++tries) {
            const int index=int(random()%npc.lines.size());
            if(index<2)continue;
            const auto& g=npc.lines[size_t(index)];
            if(g.playerClass!=characterClass&&g.playerClass!=7)continue;
            if(g.questCheck) {
                const int state=g.quest>=0&&size_t(g.quest)<questLog.flags.size()?questLog.flags[size_t(g.quest)]&1:0;
                if(state!=g.questValue)continue;
            }
            chosen=index;break;
        }
        gossipChoice[npc.monsterClass]=chosen;
    }
    gossipChosen=true;
}
// An NPC's greeting, the first time its menu opens in a game (D2Client 0x6FAF9C46 says it while the record's byte 0x15 is
// still set, then clears it). 0x6FB464A0, kind 0: the greeting, or half the time the "inactive" one; a time-of-day greeting
// when there is no other or one time in three (cycle 1 morning, 2-3 day, else evening); then one of the sound's group
// (sounds.txt Group Size, 0x6FB55310).
void greetOnce(uint32_t monsterClass) {
    static std::mt19937 random{std::random_device{}()};
    if(!greeted.insert(monsterClass).second)return;
    const auto* g=d2::findNpcGreeting(monsterClass);
    if(!g||!gameAudio||!gameSounds)return;
    int sound=g->greeting;
    if(g->inactive&&random()%2==0)sound=g->inactive;
    if(g->time&&(!sound||random()%3==0)) {
        const int cycle=worldView.envCycle;
        sound=g->time+(cycle==1?0:cycle==2||cycle==3?1:2);
    }
    if(!sound)return;
    if(const int group=gameSounds->groupByIndex(sound);group>0)sound+=int(random()%uint32_t(group));
    gameAudio->speech(gameSounds->fileByIndex(sound));
}
bool hasIntroduction(uint32_t monsterClass) {
    const auto* npc=d2::findNpcGossip(monsterClass);
    return npc&&!npc->noIntroduction&&!npc->lines.empty();
}
void startIntroduction() {
    // Entry 1 when it is for the player's class, else entry 0 (0x6FAF49C7).
    const auto* npc=d2::findNpcGossip(npcDialog.classId);
    if(!npc||npc->lines.empty())return;
    const auto& line=npc->lines.size()>1&&npc->lines[1].playerClass==characterClass?npc->lines[1]:npc->lines[0];
    startTalk(line.line);npcDialog.gossip=true;
}
void startTalk(uint16_t line) {
    npcDialog.page=NpcDialog::Page::Talk;
    npcDialog.speaking=line?line:npcDialog.lines.empty()?uint16_t(0):npcDialog.lines.front();
    npcDialog.gossip=false;
    // Nothing from the game to say: the NPC's gossip for this game (0x6FAF4403).
    if(!npcDialog.speaking)if(const auto* npc=d2::findNpcGossip(npcDialog.classId);npc&&!npc->lines.empty()) {
        if(!gossipChosen)chooseGossip();
        const int index=gossipChoice.count(npc->monsterClass)?gossipChoice[npc->monsterClass]:2;
        npcDialog.speaking=npc->lines[size_t(index)%npc->lines.size()].line;npcDialog.gossip=true;
    }
    pendingCommands.push_back(d2::Command::raw(0x2F|(9<<8),1,npcDialog.guid,0,0));
    // The voice of the first spoken line (the client's own table from line to sounds.txt row); the text scrolls for as long
    // as it speaks, or 1.5 seconds a row when the line has no voice.
    npcDialog.talkSeconds=0;
    for(const auto id:npcDialog.speaking?std::vector<uint16_t>{npcDialog.speaking}:std::vector<uint16_t>{})if(const int sound=d2::speechSound(id)) {
        if(gameAudio&&gameSounds){gameAudio->speech(gameSounds->fileByIndex(sound));npcDialog.talkSeconds=gameAudio->speechSeconds();}
        break;
    }
    if(npcDialog.talkSeconds<=0)npcDialog.talkSeconds=1.5*double(std::max<size_t>(1,talkRows().size()));
    npcDialog.talkStart=nowMilliseconds();
}
void finishTalk() {
    if(!npcDialog.open||npcDialog.page!=NpcDialog::Page::Talk)return;
    // What was said has been heard: the message goes back to the game (0x31: the NPC, the line's string id), which is what
    // moves the quests on (Akara's line 64 gives the Den of Evil, her line 76 its reward). The next Talk plays the next one.
    if(const uint16_t line=npcDialog.gossip?0:npcDialog.speaking) {
        pendingCommands.push_back(d2::Command::raw(0x31|(9<<8),npcDialog.guid,int32_t(line),0,0));
        std::erase(npcDialog.lines,line);std::erase(npcDialog.topics,line);
    }
    npcDialog.speaking=0;
    if(gameAudio)gameAudio->stopSpeech();
    npcDialog.page=NpcDialog::Page::Menu;
}
void closeNpcDialog() {
    if(!npcDialog.open)return;
    finishTalk();
    pendingCommands.push_back(d2::Command::raw(0x30|(9<<8),1,npcDialog.guid,0,0));
    if(npcDialog.openedInventory)inventoryOpen=false;
    npcDialog=NpcDialog{};sellToNpc=0;repairAtNpc=0;storeCursor=-1;
}
void handleServerPackets() {
    const auto& stream=worldView.server;
    auto u32=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
    for(size_t at=0;at+2<=stream.size();) {
        const size_t length=size_t(stream[at])|(size_t(stream[at+1])<<8);at+=2;
        if(at+length>stream.size())break;
        const uint8_t* p=&stream[at];
        if(std::getenv("D2MODERN_SERVER_LOG")&&length)std::printf("Server packet 0x%02X, %zu bytes, tick %llu\n",p[0],length,static_cast<unsigned long long>(worldView.tick));
        // D2MODERN_SERVER_DUMP=27,2A: the bytes of these packets.
        if(const char* dump=std::getenv("D2MODERN_SERVER_DUMP");dump&&length) {
            char id[8];std::snprintf(id,sizeof id,"%02X",p[0]);
            if((std::string(",")+dump+",").find(std::string(",")+id+",")!=std::string::npos) {
                std::printf("Packet %s:",id);for(size_t i=0;i<length;++i)std::printf(" %02X",p[i]);std::printf("\n");
            }
        }
        // Packet 0xA3 (D2Game sub_6FCC6300): the charge count (byte 1; SkillAss sends the new count), skill, level, caster,
        // target. For the player's progressive skill D2Client 1.10f (handler 0x6FAB2D60 → 0x6FABCD70) runs cltprgfunc for
        // that charge. Functions 12 and 13 (0x6FAC08E0, 0x6FAC09F0) play prgoverlay + (charges - 1) on the target and
        // prgsound + (charges - 1) on the caster, charges 1..3.
        if(p[0]==0xA3&&length>=24&&p[6]==0&&u32(p+7)==worldView.nativeGuid&&gameSkills)
            if(const auto* skill=gameSkills->find(uint32_t(p[2]|(p[3]<<8)))) {
                const int n=std::clamp(int(p[1]),1,3),function=skill->clientProgress[size_t(n-1)];
                if(function==12||function==13) {
                    auto numbered=[&](std::string name){if(!name.empty()&&std::isdigit(static_cast<unsigned char>(name.back())))name.back()=char('0'+n);return name;};
                    if(gameAudio&&gameSounds)if(const auto file=gameSounds->file(numbered(skill->progressSound));!file.empty())gameAudio->effect(file);
                    if(p[11]==1&&!skill->progressOverlay.empty())unitOverlays.push_back({u32(p+12),numbered(skill->progressOverlay),worldView.tick});
                    if(std::getenv("D2MODERN_SOUND_LOG"))std::cout<<"Charge "<<n<<" of skill "<<skill->id<<": "<<numbered(skill->progressSound)<<", "<<numbered(skill->progressOverlay)<<"\n";
                }
            }
        at+=length;
    }
    for(size_t at=0;at+2<=stream.size();) {
        const size_t length=size_t(stream[at])|(size_t(stream[at+1])<<8);at+=2;
        if(at+length>stream.size())break;
        const uint8_t* p=&stream[at];
        if(length>=40&&p[0]==0x27&&p[1]==1&&p[6]==1&&p[8]==3) {
            // Not a conversation: the unit says this line over its head.
            const uint32_t guid=u32(p+2);const uint16_t line=uint16_t(p[10]|(p[11]<<8));
            std::erase_if(overheadTexts,[&](const OverheadText& o){return o.guid==guid;});
            if(gameStrings)overheadTexts.push_back({guid,gameStrings->byId(line),worldView.tick+100});
        } else if(length>=20&&p[0]==0x81&&u32(p+4)==worldView.nativeGuid) {
            // D2GSPacketSrv81: this player's hireling (its type, monstats class, owner, unit, seed, name string).
            mercClass=uint32_t(p[2]|(p[3]<<8));mercGuid=u32(p+8);mercName=uint32_t(p[16]|(p[17]<<8));
            std::erase_if(pets,[&](const Pet& pet){return pet.guid==mercGuid;});
            pets.push_back({p[1],mercClass,mercGuid,mercName});
        } else if(length>=7&&p[0]==0x9B) {
            deadMercName=int16_t(p[1]|(p[2]<<8));resurrectCost=int32_t(u32(p+3));
        } else if(length>=13&&p[0]==0x7A&&(p[1]==0||u32(p+5)==worldView.nativeGuid)) {
            // D2GSPacketSrv7A: a pet of this player comes (1: its pettype row, monstats class, owner, unit) or goes (0: only the unit).
            const uint32_t guid=u32(p+9);
            std::erase_if(pets,[&](const Pet& pet){return pet.guid==guid;});
            if(p[1]==1)pets.push_back({p[2],uint32_t(p[3]|(p[4]<<8)),guid,0});
            else if(guid==mercGuid)mercGuid=0;
        } else if(length>=40&&p[0]==0x27&&p[1]==1) {
            npcDialog=NpcDialog{};npcDialog.open=true;npcDialog.guid=int32_t(u32(p+2));
            if(!gossipChosen)pendingCommands.push_back(d2::Command::raw(0x40|(1<<8),0,0,0,0)); // the quest states the gossip choice reads (0x28)
            for(int i=0;i<std::min<int>(p[6],8);++i) {
                // D2MessageStrc: the menu type, then the string. Talk says types 0 and 2 (TEXT_GetStringIdOfMenuType0Or2Node), the
                // menu lists type 2 under their captions (D2Client 0x6FAF3FE8); type 1 is only offered as an entry elsewhere.
                const uint8_t menu=p[8+4*i];const uint16_t id=uint16_t(p[8+4*i+2]|(p[8+4*i+3]<<8));
                if(menu!=1)npcDialog.lines.push_back(id);
                if(menu==2)npcDialog.topics.push_back(id);
            }
            for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(npcDialog.guid)){npcDialog.classId=e.classId;npcDialog.services=e.services;}
            greetOnce(npcDialog.classId);
        } else if(length>=15&&p[0]==0x2A) {
            const int code=p[2];
            // A store's answer (D2Client 0x6FAF58E4): sold (1), the gold's sound (item_gold); bought (0), the bought item's
            // drop sound, chosen as the request went out (0x6FAFA865, items.txt dropsound).
            if(gameAudio&&gameSounds) {
                std::string sound;
                if(code==1)sound="item_gold";
                else if(code==0&&boughtItemCode&&gameItems)if(const auto* info=gameItems->find(boughtItemCode))sound=info->dropSound;
                if(code==0)boughtItemCode=0;
                if(const auto file=sound.empty()?std::string():gameSounds->file(sound);!file.empty())gameAudio->effect(file);
            }
            gameMessage=code==0||code==2||code==5?"":code==1?"Sold.":code==12?"Not enough gold.":code==10?"Your inventory is full.":"That cannot be done.";
            gameMessageUntil=worldView.tick+100;
        } else if(length>=1&&p[0]==0x97) {
            // The weapon sets were swapped (also sent as a game starts on set II): D2Client 0x6FAED280 flips its flag
            // 0x6FBB594C, which the inventory shows with the I / II tabs.
            secondWeaponSet=!secondWeaponSet;
        } else if(length>=2&&p[0]==0x77) {
            // The game opens or closes a window: 16 the stash, 17 closes it, 21 the Horadric Cube.
            if(p[1]==16){stashOpen=true;cubeOpen=false;inventoryOpen=true;}
            else if(p[1]==17)stashOpen=false;
            else if(p[1]==21){cubeOpen=true;stashOpen=false;inventoryOpen=true;}
        } else if(length>=12&&p[0]==0x0E&&p[1]==2) {
            const uint32_t guid=u32(p+2),mode=u32(p+8);
            if(mode>0&&mode<8)if(auto c=objectClasses.find(guid);c!=objectClasses.end()&&persistentOpenedContainer(c->second,nullptr))openedObjectModes[guid]=2;
        } else if(length>=8&&p[0]==0x11) {
            // UNITS_SetOverlay (SUnitMsg sub_6FCC5F20): skills.txt srvoverlay, a missile's ProgOverlay, life leech.
            // A monster is drawn by its native id. The player is drawn as unit 0; the packet carries the native guid.
            const uint32_t unit=u32(p+2),row=uint32_t(p[6]|(p[7]<<8));
            const std::string name=gameOverlays?gameOverlays->nameOfRow(row):std::string();
            if(std::getenv("D2MODERN_OVERLAY_LOG"))std::cout<<"Overlay "<<row<<" ("<<name<<") on unit type "<<int(p[1])<<" #"<<unit<<"\n";
            uint32_t drawn=~0u;
            if(p[1]==1)drawn=unit;
            else if(p[1]==0&&unit==worldView.nativeGuid)drawn=0;
            if(drawn!=~0u&&!name.empty()&&name!="null")unitOverlays.push_back({drawn,name,worldView.tick});
        } else if(length>=0x67&&p[0]==0x28&&p[1]==6) {
            // The quest flags, 16 bits per quest (the quest log's first answer).
            for(size_t i=0;i<questLog.flags.size();++i)questLog.flags[i]=uint16_t(p[7+2*i]|(p[7+2*i+1]<<8));
            questLog.known=true;
        } else if(length>=42&&p[0]==0x52) {
            for(size_t i=0;i<questLog.state.size();++i)questLog.state[i]=p[1+i];
        } else if(length>=5&&p[0]==0x50&&p[1]==1&&p[2]==0) {
            questLog.monstersLeft=p[3]|(p[4]<<8);
        } else if(length>=21&&p[0]==0x63) {
            waypointMenu=WaypointMenu{};waypointMenu.open=true;waypointMenu.guid=int32_t(u32(p+1));
            for(int i=0;i<8;++i)waypointMenu.flags[i]=uint16_t(p[5+2*i]|(p[5+2*i+1]<<8));
        } else if(length>=8&&p[0]==0x3F) {
            // D2GSPacketSrv3F (SCmd SendPacket0x3F): nCursor 0xFF clears the spell cursor. Identify's spellicon is 0,
            // frame 0 of cursor\buysell. Both the clear and the set can arrive in one reply; they are applied in order.
            // cursor\identifyready.wav is sounds.txt index 12 (cursor_identify_ready), only for that frame.
            if(p[1]==0xFF)clearIdentifyCursor();
            else {
                pendingIdentifyItem=int32_t(u32(p+2));spellCursor=int(p[1]);cursorPressed=nowMilliseconds();
                if(p[1]==0&&gameAudio)gameAudio->effect("cursor\\identifyready.wav");
            }
        } else if(length>=8&&p[0]==0x2C) {
            // D2GSPacketSrv2C: nSoundEvent 4 is the cube's convert (PLRTRADE_CreateCubeOutputs), 6 identify, 19 the character's "impossible" (D2Game sends it when it refuses:
            // no gem for a gem shrine, gold the stash cannot give, a quest item that does not fit).
            const uint16_t sound=uint16_t(p[6]|(p[7]<<8));
            if(std::getenv("D2MODERN_SOUND_EVENT_LOG"))std::cout<<"Sound event "<<sound<<" on unit type "<<int(p[1])<<" #"<<u32(p+2)<<"\n";
            if(sound==4&&p[1]==0&&u32(p+2)==worldView.nativeGuid&&gameAudio)gameAudio->effect("cursor\\convert.wav");
            else if(sound==6&&p[1]==0&&u32(p+2)==worldView.nativeGuid&&gameAudio)gameAudio->effect("cursor\\identify.wav");
            else if(sound==19&&p[1]==0&&u32(p+2)==worldView.nativeGuid)pendingImpossibleSound=true;
        }
        at+=length;
    }
}
// The enemy whose sprite is under a point given in scene pixels (sprites are roughly 44 wide and 87 tall).
const d2::EntitySnapshot* pickUnit(float x,float y,d2::EntityKind kind) {
    const d2::EntitySnapshot* hit=nullptr;float best=1e9f;
    if(!walkScene)return nullptr;
    for(const auto& e:worldView.entities) {
        if(e.kind!=kind)continue;
        const auto a=d2::projectPoint(*walkScene,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne);
        const float dx=std::abs(x-a[0]),dy=y-(a[1]-30);
        if(dx<=22&&dy>=-45&&dy<=42&&dx+std::abs(dy)<best){best=dx+std::abs(dy);hit=&e;}
    }
    return hit;
}
const d2::EntitySnapshot* pickEnemy(float x,float y){return pickUnit(x,y,d2::EntityKind::Enemy);}
const d2::EntitySnapshot* pickTownPerson(float x,float y){return pickUnit(x,y,d2::EntityKind::TownPerson);}
// A player's body lying dead (mode 17; a corpse holding its gear, or another player down) under the cursor: lying, so a wide
// flat box around its feet.
const d2::EntitySnapshot* pickCorpse(float x,float y) {
    const d2::EntitySnapshot* hit=nullptr;float best=1e9f;
    if(!walkScene)return nullptr;
    for(const auto& e:worldView.entities) {
        if(e.kind!=d2::EntityKind::Player||e.mode!=17||e.id==worldView.localPlayer)continue;
        const auto a=d2::projectPoint(*walkScene,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne);
        const float dx=std::abs(x-a[0]),dy=y-(a[1]-10);
        if(dx<=40&&dy>=-22&&dy<=18&&dx+std::abs(dy)<best){best=dx+std::abs(dy);hit=&e;}
    }
    return hit;
}
void saveRequestedShot(const std::function<void(const std::filesystem::path&)>& save) {
    if(shotRequest<0)return;
    if(const auto dir=d2::environmentPath("D2MODERN_SHOT_DIR");!dir.empty())save(dir/("shot"+std::to_string(shotRequest)+".bmp"));
    shotRequest=-1;
}
// Leaving a game for the main menu: the client's state goes back to how it is before any game (the window and the
// command line's choices stay). Pointers into the game that ended (its scene, tables, sounds) are cleared; the next
// game sets them again.
void resetClientState() {
    touch::reset(); // first: letting go of the controls may queue orders, cleared below
    frameStep=0;panX=panY=0;dragX=dragY=0;dragging=false;
    showCollision=false;followPlayer=false;runEnabled=false;
    walkGrid=nullptr;walkScene=nullptr;
    worldView=d2::WorldSnapshot{};pendingCommands.clear();
    actionTargetUnit=0;actionTargetIsPoint=false;actionTargetPoint={};heldFacing=-1;
    castSkill=0;castStart=0;lastPlayerMode=255;walkStatus="Ready";
    gameMenuPage=GameMenuPage::Closed;gameMenuRegions.clear();gameMenuSelection=0;gameMenuPressed=0;leaveToMainMenu=false;
    gameAudio=nullptr;gameSounds=nullptr;gameStrings=nullptr;gameItems=nullptr;gameMissiles=nullptr;gameSkills=nullptr;gameUnitArt=nullptr;
    clientEffects.clear();clientLights.clear();gameOverlays=nullptr;gameStates=nullptr;unitOverlays.clear();objectClocks.clear();objectClasses.clear();openedObjectModes.clear();itemLabelUntil.clear();
    objectPicks.clear();hoveredObject=0;unitPicks.clear();hoveredUnit=0;afterTick=nullptr;
    leftButtonDown=rightButtonDown=false;itemLabelBoxes.clear();
    inventoryRegions.clear();inventoryOpen=false;beltRegions.clear();beltOpen=false;
    panelX=panelY=panelW=panelH=0;cursorItem=0;cursorItemW=cursorItemH=1;beltPressed=false;scriptedShift=false;
    std::fill(&inventoryOccupied[0][0],&inventoryOccupied[0][0]+4*10,0);
    clearIdentifyCursor();sellToNpc=0;repairAtNpc=0;storeCursor=-1;secondWeaponSet=false;itemDrag=ItemDrag{};gossipChoice.clear();gossipChosen=false;greeted.clear();
    stashOpen=cubeOpen=false;storageRegions.clear();storageX=storageY=storageW=storageH=0;
    questLog=QuestLog{};questRegions.clear();goldPopup=GoldPopup{};goldRegions.clear();
    std::fill(&storageOccupied[0][0],&storageOccupied[0][0]+8*6,0);
    npcDialog=NpcDialog{};gameMessage.clear();gameMessageUntil=0;
    waypointMenu=WaypointMenu{};waypointRegions.clear();waypointPanelX=waypointPanelY=waypointPanelW=waypointPanelH=0;
    dialogRegions.clear();dialogX=dialogY=dialogW=dialogH=0;
    skillsOpen=characterOpen=false;miniPanelOpen=false;automapOpen=false;
    sceneRooms.clear();sceneOrigin={};skillTab=2;skillMenu=0;characterClass=-1;characterName.clear();pets.clear();mercGuid=0;deadMercName=-1;overheadTexts.clear();
    skillRegions.clear();skillPanelX=skillPanelY=skillPanelW=skillPanelH=0;characterPanelX=characterPanelY=characterPanelW=characterPanelH=0;
    leftHeld=false;frontEnd=nullptr;
}
