#include "NativeGame.h"
#include "NativeProtocol.h"
#include "SkillVisuals.h"
#ifdef D2MODERN_INPROCESS
#include "NativeSession.h" // the game runs in this process (D2MOO linked in); calls are direct
#include "Lan.h"
#endif
#ifndef D2MODERN_INPROCESS
#include <windows.h>        // otherwise it runs in NativeGameHost.exe, over pipes
#endif
#include "ProgramPath.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <cmath>
#include <deque>
#include <optional>
#include <unordered_map>
#include <stdexcept>
namespace d2 {
namespace {
#ifndef D2MODERN_INPROCESS
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
std::wstring quote(const std::filesystem::path& path) {
    // Windows argv escaping, including trailing backslashes before the closing quote.
    std::wstring out=L"\"";size_t slashes=0;
    for(auto c:path.wstring()) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');
        slashes=0;out+=c;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
#endif
class NativeGame final:public GameBackend {
public:
    bool setScene(const CollisionGrid& grid,GridPoint origin,std::vector<int> levels) override {grid_=grid;origin_=origin;levels_=std::move(levels);target_.reset();destination_.reset();
#ifdef D2MODERN_INPROCESS
        shownValid_=false;shownGoal_.reset();
#endif
        return true;}
private:
#ifdef D2MODERN_INPROCESS
    std::unique_ptr<native::Session> session_;
    std::unique_ptr<native::Reply> reply_=std::make_unique<native::Reply>();
    native::Request request_;bool answered_=true; // the first reply comes from starting the session
    // LAN: hosting (the session takes in guests and steps the game for everyone) or joining (no session here: the
    // requests go to the host and its snapshots come back).
    std::unique_ptr<lan::Host> lanHost_;
    std::unique_ptr<lan::Guest> lanGuest_;
    std::filesystem::path lanSaveOut_; // where a guest writes its character when it leaves
    native::Reply receive(unsigned) {
        if(!answered_) {
            answered_=true;
            if(lanGuest_){lanGuest_->send(request_);lanGuest_->receive(*reply_);}
            else {
                if(lanHost_)lanHost_->beforeTick(*session_);
                session_->handle(request_,*reply_);
                if(lanHost_)lanHost_->afterTick(*session_);
            }
        }
        return *reply_;
    }
    void send(native::Request request) {request_=request;answered_=false;}
#else
    Handle process_,job_,input_,output_;
#endif
    CollisionGrid grid_;
    GridPoint origin_,held_{};
    std::vector<int> levels_;
    std::optional<GridPoint> target_;
    std::optional<int32_t> attack_; // native unit number being attacked; re-sent while it stays alive
    bool attackNow_=false,respawn_=false,running_=false;
    std::optional<int32_t> pickup_; // native item number to pick up next tick
    std::optional<int32_t> interact_; // native unit number of a town person to walk up to and talk to
    std::optional<int32_t> interactObject_; // native object number to walk up to and open/use
    std::optional<GridPoint> destination_; // far walk target, followed in hops the game pathfinder can reach
    int destinationTicks_=0;
    std::optional<GridPoint> walkGoal_; // the last walk target sent (scene subtiles): the player faces it while moving
    // The next point on the way to goal: the goal itself when it is close, else a waypoint on a path over the scene grid (the native pathfinder reaches about 14 subtiles).
    GridPoint hopToward(GridPoint here,GridPoint goal) const {
        if(std::max(std::abs(goal.x-here.x),std::abs(goal.y-here.y))<=14)return goal;
        const auto path=findPath(grid_,here,goal);
        if(path.size()<=1)return goal;
        // The farthest point of the path (within reach) that a straight line gets to: the grid path zigzags between
        // diagonal and straight steps, and aiming at its points made the character turn back and forth.
        auto clear=[&](GridPoint to) {
            const int steps=std::max(std::abs(to.x-here.x),std::abs(to.y-here.y))*2;
            for(int i=1;i<=steps;++i) {
                const GridPoint at{here.x+int(std::lround(double(to.x-here.x)*i/steps)),here.y+int(std::lround(double(to.y-here.y)*i/steps))};
                if(!grid_.walkable(at))return false;
            }
            return true;
        };
        // Open ground: straight toward the goal, 14 subtiles at a time.
        if(clear(goal)) {
            const double distance=std::max(std::abs(goal.x-here.x),std::abs(goal.y-here.y));
            return GridPoint{here.x+int(std::lround((goal.x-here.x)*14/distance)),here.y+int(std::lround((goal.y-here.y)*14/distance))};
        }
        for(size_t i=std::min<size_t>(path.size()-1,14);i>1;--i)
            if(std::max(std::abs(path[i].x-here.x),std::abs(path[i].y-here.y))<=14&&clear(path[i]))return path[i];
        return path[1];
    }
    struct QueuedPacket {native::Request request;bool usePlayerPosition=false;};
    std::deque<QueuedPacket> packets_; // inventory packets, one per tick
    WorldSnapshot view_;
    uint32_t guid_{};
#ifndef D2MODERN_INPROCESS
    native::Reply receive(DWORD timeout) {
        native::Reply reply;auto* bytes=reinterpret_cast<uint8_t*>(&reply);DWORD total=0;
        const auto deadline=GetTickCount64()+timeout;
        while(total<sizeof(reply)) {
            DWORD available=0;
            if(!PeekNamedPipe(output_.value,nullptr,0,nullptr,&available,nullptr))throw std::runtime_error("Native game host closed its pipe; see native-game-host.log");
            if(available) {
                DWORD got=0;
                if(!ReadFile(output_.value,bytes+total,std::min<DWORD>(available,sizeof(reply)-total),&got,nullptr)||!got)throw std::runtime_error("Native snapshot read failed");
                total+=got;
            } else {
                if(WaitForSingleObject(process_.value,5)==WAIT_OBJECT_0)throw std::runtime_error("Native game host exited; see native-game-host.log");
                if(GetTickCount64()>deadline)throw std::runtime_error("Native game host timed out");
            }
        }
        if(reply.signature!=native::magic)throw std::runtime_error("Native game returned an invalid snapshot");
        return reply;
    }
    void send(native::Request request) {
        DWORD done=0;
        if(!WriteFile(input_.value,&request,sizeof(request),&done,nullptr)||done!=sizeof(request))throw std::runtime_error("Native command pipe closed");
    }
#endif
    // The server keeps no usable facing for monsters, so it is derived: from the step just taken, toward the player
    // while attacking or casting, and otherwise unchanged. 0 is screen-down (+x,+y); steps of 22.5 degrees.
    static unsigned facingToward(double dx,double dy) {
        const double degrees=std::atan2(dy,dx)*180.0/3.14159265358979323846;
        int step=int(std::lround((degrees-45.0)/22.5));
        return unsigned(((step%16)+16)%16);
    }
    static bool isHydraHead(uint32_t classId) {
        return classId>=351&&classId<=353; // monstats.txt hydra1, hydra2, hydra3
    }
    void publishItems(const native::Reply& reply) {
        view_.server.assign(reply.server,reply.server+std::min<uint32_t>(reply.serverBytes,native::maxServerBytes));
        std::copy(std::begin(reply.stats),std::end(reply.stats),view_.stats.begin());
        {
            const auto& s=reply.sheet;auto& d=view_.sheet;
            d.damageMin=s.damageMin;d.damageMax=s.damageMax;d.attackRating=s.attackRating;d.defense=s.defense;
            std::copy(std::begin(s.resist),std::end(s.resist),d.resist.begin());std::copy(std::begin(s.resistMax),std::end(s.resistMax),d.resistMax.begin());
            std::copy(std::begin(s.baseAttribute),std::end(s.baseAttribute),d.baseAttribute.begin());
            std::copy(std::begin(s.baseMax),std::end(s.baseMax),d.baseMax.begin());
            for(int i=0;i<2;++i){d.skillDamageMin[size_t(i)]=s.skillDamageMin[i];d.skillDamageMax[size_t(i)]=s.skillDamageMax[i];d.skillAttack[size_t(i)]=s.skillAttack[i];}
        }
        view_.objects.clear();view_.tiles.clear();
        for(uint32_t i=0;i<reply.objectCount&&i<native::maxObjects;++i){const auto& o=reply.objects[i];(o.flags&1?view_.tiles:view_.objects).push_back({o.guid|(o.flags&1?tileIdBit:0u),o.classId,o.mode,o.x-origin_.x,o.y-origin_.y,o.flags});}
        view_.leftSkill=reply.leftSkill;view_.rightSkill=reply.rightSkill;view_.skills.clear();
        for(uint32_t i=0;i<reply.skillCount&&i<native::maxSkills;++i)view_.skills.push_back({reply.skills[i].id,reply.skills[i].level,reply.skills[i].baseLevel,reply.skills[i].quantity});
        view_.items.clear();
        for(uint32_t i=0;i<reply.itemCount&&i<native::maxItems;++i) {
            const auto& it=reply.items[i];
            ItemSnapshot s;s.id=it.guid;s.classId=it.classId;s.code=it.code;s.mode=it.mode;
            s.x=it.mode==3||it.mode==5?(it.x*fixedOne-origin_.x*fixedOne)/fixedOne:it.x;s.y=it.mode==3||it.mode==5?(it.y*fixedOne-origin_.y*fixedOne)/fixedOne:it.y;
            s.quality=it.quality;s.flags=it.flags;s.invGraphic=it.invGraphic;s.prefix=it.prefix;s.suffix=it.suffix;s.rarePrefix=it.rarePrefix;s.rareSuffix=it.rareSuffix;s.fileIndex=it.fileIndex;s.page=it.page;s.bodyLoc=it.bodyLoc;s.vendor=it.owner;s.price=it.price;
            s.minDamage=it.minDamage;s.maxDamage=it.maxDamage;s.twoHandMinDamage=it.twoHandMinDamage;s.twoHandMaxDamage=it.twoHandMaxDamage;s.throwMinDamage=it.throwMinDamage;s.throwMaxDamage=it.throwMaxDamage;
            s.armorClass=it.armorClass;s.durability=it.durability;s.maxDurability=it.maxDurability;s.quantity=it.quantity;s.traits=it.traits;s.sockets=it.sockets;s.parent=it.parent;
            s.invTrans=it.invTrans;s.invColor=it.invColor;s.chrTrans=it.chrTrans;s.chrColor=it.chrColor;
            s.armorTotal=it.armorTotal;s.reqStr=it.reqStr;s.reqDex=it.reqDex;s.reqLevel=it.reqLevel;s.reqMet=it.reqMet;
            for(uint32_t k=0;k<std::min(it.statCount,native::maxItemStats);++k)s.stats.push_back({it.stats[k].id,it.stats[k].param,it.stats[k].value});
            for(uint32_t k=it.statCount;k<std::min(it.statCount+it.setStatCount,native::maxItemStats);++k)s.setStats.push_back({it.stats[k].id,it.stats[k].param,it.stats[k].value});
            view_.items.push_back(s);
        }
        std::copy(std::begin(reply.bodyPlacement),std::end(reply.bodyPlacement),view_.bodyPlacement.begin());
        view_.setBonuses.clear();
        for(const auto& bonus:reply.setBonuses)if(bonus.set>=0) {
            WorldSnapshot::SetBonus out;out.set=bonus.set;
            for(uint32_t k=0;k<std::min(bonus.statCount,16u);++k)out.stats.push_back({bonus.stats[k].id,bonus.stats[k].param,bonus.stats[k].value});
            view_.setBonuses.push_back(std::move(out));
        }
        view_.goldLost=reply.goldLost;
        view_.ownedSetItems.assign(reply.ownedSetItems,reply.ownedSetItems+std::min(reply.ownedSetItemCount,native::maxOwnedSetItems));
        view_.wieldType=reply.wieldType;view_.bodyFits=reply.bodyFits;
    }
    void publishUnits(const native::Reply& reply) {
        std::unordered_map<EntityId,EntitySnapshot> old;
        for(size_t i=1;i<view_.entities.size();++i)old[view_.entities[i].id]=view_.entities[i];
        view_.entities.resize(1);
        const auto player=view_.entities[0].position;
        for(uint32_t i=0;i<reply.unitCount&&i<native::maxUnits;++i) {
            const auto& u=reply.units[i];
            EntitySnapshot e;e.id=(1000+u.guid)|((u.flags&native::unitMissile)?missileIdBit:0u)|((u.flags&native::unitPlayer)?playerIdBit:0u);
            e.position={u.x-origin_.x*fixedOne,u.y-origin_.y*fixedOne};e.previous=e.position;
            e.kind=(u.flags&native::unitPlayer)?EntityKind::Player:(u.flags&native::unitMissile)?EntityKind::Missile:(u.flags&native::unitDead)?EntityKind::Corpse:(u.flags&native::unitTownPerson)?EntityKind::TownPerson:EntityKind::Enemy;
            e.services=uint8_t(((u.flags&native::unitVendor)?1:0)|((u.flags&native::unitGambler)?2:0));
            e.classId=u.classId;e.life=u.life;e.maxLife=u.maxLife;e.mode=uint8_t(u.mode);e.direction=uint8_t(u.direction&63);e.animSpeed=uint16_t(u.animSpeed);std::copy(std::begin(u.states),std::end(u.states),e.states.begin());
            e.monsterType=u.monsterType;e.colorTransform=u.colorTransform;e.hitClass=u.hitClass;
            std::copy(std::begin(u.umods),std::end(u.umods),e.umods.begin());std::copy(std::begin(u.name),std::end(u.name),e.name.begin());
            std::copy(std::begin(u.components),std::end(u.components),e.components.begin());
            e.lookClass=u.lookClass<7?uint8_t(u.lookClass):uint8_t(255);e.lookMode=uint8_t(u.lookMode);std::copy(std::begin(u.look),std::end(u.look),e.look.begin());
            // Native missile path direction is a 64-step circle. Use it immediately so projectiles do not draw with an
            // arbitrary first-frame facing before position deltas are available.
            e.facing=e.kind==EntityKind::Missile?uint8_t((u.direction>>2)&15):uint8_t((u.guid*6)&15);
            if(auto it=old.find(e.id);it!=old.end()) {
                e.previous=it->second.position;e.facing=it->second.facing;
                e.moving=e.previous!=e.position;
                const double dx=double(e.position.x-e.previous.x),dy=double(e.position.y-e.previous.y);
                if(dx*dx+dy*dy>double(fixedOne)*fixedOne*0.0004) {
                    // Walking: toward where the path goes (the original turns a monster to its move packet's target), unless
                    // it is about there; each step's own direction zigzags on an angled path.
                    double fx=dx,fy=dy;
                    if(e.kind!=EntityKind::Missile&&e.kind!=EntityKind::Player&&(u.targetX||u.targetY)) {
                        const double tx=(double(u.targetX)+.5-origin_.x)*fixedOne-double(e.position.x),ty=(double(u.targetY)+.5-origin_.y)*fixedOne-double(e.position.y);
                        if(tx*tx+ty*ty>double(fixedOne)*fixedOne){fx=tx;fy=ty;}
                    }
                    e.facing=uint8_t(facingToward(fx,fy));
                }
                else if(e.kind==EntityKind::Enemy&&(e.mode==4||e.mode==5||e.mode==7||(e.mode>=8&&e.mode<=11)))
                    e.facing=uint8_t(facingToward(double(player.x-e.position.x),double(player.y-e.position.y)));
                // Ticks since the animation mode last changed drive the sprite frame.
                e.stateTicks=e.mode==it->second.mode?it->second.stateTicks+1:0;
                e.skillMoveFrame=it->second.skillMoveFrame;
            }
            if(e.kind==EntityKind::Player) {
                // Another player (LAN) in a skill's sequence (Charge, Leap...): drawn as this player's own character is. Leap's
                // flight and landing frames are stepped here, a tick at a time, from its path (as publish does for this player).
                e.sequenceMode=u.sequenceMode;e.sequenceFrame=u.sequenceFrame;e.usedSkill=u.usedSkill;
                if(u.pathType==9)e.skillMoveFrame=nextLeapSequenceFrame(e.skillMoveFrame,true,e.mode);
                else if(e.skillMoveFrame!=255)e.skillMoveFrame=nextLeapSequenceFrame(e.skillMoveFrame,false,e.mode);
            }
            // Hydra heads are stationary turrets. Their fire direction is stored in D2Game's path direction; deriving facing
            // from movement or from the player makes the head point the wrong way when it shoots.
            if(e.kind==EntityKind::Enemy&&isHydraHead(e.classId))e.facing=uint8_t((u.direction>>2)&15);
            view_.entities.push_back(e);
        }
    }
    static std::array<uint32_t,17> appearanceCodes(const native::Reply& reply) {
        std::array<uint32_t,17> codes{};codes[0]=reply.weaponClass;
        // A component's colour rides in its code's top byte (the token takes three): trans * 21 + colour + 1, 0 none.
        for(size_t i=0;i<16;++i)codes[i+1]=(reply.components[i]&0xffffff)|(reply.componentTrans[i]?uint32_t(reply.componentTrans[i]*21+reply.componentColor[i]+1)<<24:0u);
        return codes;
    }
    void publish(const native::Reply& reply,bool first) {
        FixedPoint position{reply.x-origin_.x*fixedOne,reply.y-origin_.y*fixedOne};
        // A save can begin in a different act than the boot scene. Main swaps in the correct scene as soon as it sees
        // reply.level; until then the first relative position may not be walkable in this temporary grid.
        if(first) {guid_=reply.guid;view_={0,1,reply.level,{reply.life,reply.maxLife,reply.mana,reply.maxMana,reply.experience,int32_t(reply.characterLevel),reply.gold,reply.stashGold,reply.potionLife,reply.potionMana},{{1,position,position,0,false,0,reply.animationFrame}},{}};view_.nativeGuid=reply.guid;view_.tick=reply.tick;view_.appearance=appearanceCodes(reply);publishUnits(reply);publishItems(reply);return;} // a LAN guest joins a game already under way
        if(reply.guid!=guid_||reply.tick!=view_.tick+1)throw std::runtime_error("Native player identity/tick changed unexpectedly (player "+std::to_string(reply.guid)+
            " after "+std::to_string(guid_)+", tick "+std::to_string(reply.tick)+" after "+std::to_string(view_.tick)+")");
        view_.nativeGuid=reply.guid;
        auto& p=view_.entities[0];p.previous=p.position;p.position=position;
        const bool moved=p.previous!=p.position;
        // Facing, in the animations' 16 directions: toward where the player was sent, as the game's client does (the server
        // moves along short segments, and facing each step turned the character back and forth); right at the target, the
        // way the step went. (The sign of each axis alone, as before, gave only 8 directions.)
        if(moved) {
            double dx=double(position.x-p.previous.x),dy=double(position.y-p.previous.y);
            if(walkGoal_) {
                const double gx=(walkGoal_->x+.5)*fixedOne-position.x,gy=(walkGoal_->y+.5)*fixedOne-position.y;
                if(gx*gx+gy*gy>double(fixedOne)*fixedOne){dx=gx;dy=gy;}
            }
            p.facing=uint8_t(facingToward(dx,dy));
        }
        // Movement is observed from authoritative positions, not inferred from accepted input.
        p.stateTicks=moved==p.moving?p.stateTicks+1:0;p.moving=moved;view_.tick=reply.tick;view_.level=reply.level;view_.envCycle=reply.envCycle;view_.envColor={reply.envRed,reply.envGreen,reply.envBlue};view_.envIntensity=reply.envIntensity;
        {const auto& h=reply.hireling;view_.hireling={h.guid,h.level,h.experience,h.nextExperience,h.strength,h.dexterity,h.life,h.maxLife,h.defense,h.minDamage,h.maxDamage,{h.resist[0],h.resist[1],h.resist[2],h.resist[3]},h.baseStrength,h.baseDexterity};}
        view_.hirelings.clear();for(uint32_t i=0;i<std::min(reply.hirelingCount,native::maxHirelings);++i){const auto& h=reply.hirelings[i];view_.hirelings.push_back({h.name,h.description,h.level,h.life,h.defense,h.cost,h.minDamage,h.maxDamage,h.resist});}view_.vitals={reply.life,reply.maxLife,reply.mana,reply.maxMana,reply.experience,int32_t(reply.characterLevel),reply.gold,reply.stashGold,reply.potionLife,reply.potionMana};
        p.animationFrame=reply.animationFrame;p.mode=uint8_t(reply.mode);p.sequenceMode=uint8_t(std::min<uint32_t>(reply.sequenceMode,255));p.sequenceFrame=uint8_t(reply.sequenceFrame);p.frameEvent=reply.frameEvent!=0;p.shapeClass=uint16_t(std::min<uint32_t>(reply.shapeClass,0xFFFF));p.shapeMode=uint8_t(reply.shapeMode);std::copy(std::begin(reply.playerStates),std::end(reply.playerStates),p.states.begin());view_.appearance=appearanceCodes(reply);
        const FixedPoint pathTarget{(reply.pathTargetX-origin_.x)*fixedOne,(reply.pathTargetY-origin_.y)*fixedOne};
        if(reply.pathType==9) {
            if(!p.skillMovePath||p.skillMoveTarget!=pathTarget)p.skillMoveOrigin=p.previous;
            p.skillMoveFrame=nextLeapSequenceFrame(p.skillMoveFrame,true,p.mode);
            p.skillMovePath=true;p.skillMoveTarget=pathTarget;
        } else if(p.skillMoveFrame!=255) {
            p.skillMoveFrame=nextLeapSequenceFrame(p.skillMoveFrame,false,p.mode);
            if(p.mode!=18)p.skillMovePath=false;
        }
        publishUnits(reply);publishItems(reply);
    }
public:
    uint32_t seed{},characterClass{};
    CharacterAppearance appearance;
    bool coloredEquipment=false;
    NativeGame(const std::filesystem::path& dlls,const std::filesystem::path& game,const std::filesystem::path& save,uint32_t requestedSeed,const CollisionGrid& grid,GridPoint origin,std::vector<int> levels,const std::filesystem::path& saveOut={},const std::function<void()>& waiting={})
        :grid_(grid),origin_(origin),levels_(std::move(levels)) {
#ifdef D2MODERN_INPROCESS
        native::Session::Settings settings;
        settings.dlls=dlls;settings.game=game;settings.save=save;settings.seed=requestedSeed;settings.saveOut=saveOut;
        settings.log=programDirectory()/"native-game-host.log"; // where the separate host's messages went
        if(const char* join=std::getenv("D2MODERN_LAN_JOIN");join&&*join) {
            // A LAN guest: the character goes to the host, which plays it in its game.
            std::ifstream input(save,std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
            lanGuest_=std::make_unique<lan::Guest>(join,bytes,waiting);lanSaveOut_=saveOut;
            lanGuest_->receive(*reply_);
            std::printf("LAN: joined %s as client %d\n",join,int(lanGuest_->client()));std::fflush(stdout);
        } else {
            session_=native::Session::start(settings,*reply_);
            if(const char* host=std::getenv("D2MODERN_LAN_HOST");host&&*host)lanHost_=std::make_unique<lan::Host>(uint16_t(std::atoi(host)>0?std::atoi(host):lan::defaultPort));
        }
#else
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        Handle childInput,childOutput;
        if(!CreatePipe(&childInput.value,&input_.value,&security,0)||!CreatePipe(&output_.value,&childOutput.value,&security,0))throw std::runtime_error("Cannot create native host pipes");
        SetHandleInformation(input_.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(output_.value,HANDLE_FLAG_INHERIT,0);
        wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
        const auto directory=std::filesystem::path(exe).parent_path();
        Handle log;log.value=CreateFileW((directory/L"native-game-host.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(log.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open native game host log");
        job_.value=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job_.value||!SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot create native host lifetime job");
        std::wstring command=quote(directory/L"NativeGameHost.exe")+L" "+quote(std::filesystem::absolute(dlls))+L" "+quote(std::filesystem::absolute(game))+L" "+quote(std::filesystem::absolute(save))+L" "+std::to_wstring(requestedSeed)+(saveOut.empty()?std::wstring():L" "+quote(std::filesystem::absolute(saveOut)));
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;
        start.hStdInput=childInput.value;start.hStdOutput=childOutput.value;start.hStdError=log.value;
        PROCESS_INFORMATION info{};
        if(!CreateProcessW((directory/L"NativeGameHost.exe").c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,directory.c_str(),&start,&info))throw std::runtime_error("Cannot start NativeGameHost.exe");
        process_.value=info.hProcess;Handle thread;thread.value=info.hThread;
        if(!AssignProcessToJobObject(job_.value,process_.value)){TerminateProcess(process_.value,1);throw std::runtime_error("Cannot bind native host lifetime");}
        ResumeThread(thread.value);
        // Parent must not retain the child's pipe ends or EOF detection will hang.
        CloseHandle(childInput.value);childInput.value=nullptr;CloseHandle(childOutput.value);childOutput.value=nullptr;
#endif
        auto reply=receive(30000);seed=reply.seed;characterClass=reply.characterClass;
        coloredEquipment=reply.coloredComponents!=0;
        appearance=appearanceFromCodes(characterClass,appearanceCodes(reply));
        if(!lanGuest_&&seed!=requestedSeed)throw std::runtime_error("Native game seed does not match generated town");
        publish(reply,true);
    }
    ~NativeGame() override {
#ifdef D2MODERN_INPROCESS
        // Leaving the game saves the character (cbSaveCharacter).
        if(lanGuest_){try{lanGuest_->leave(lanSaveOut_);}catch(...){}} // the host sends back the character as the game saved it
        lanHost_.reset(); // guests' connections close with the game
        if(session_){try{session_->handle({native::magic,native::Operation::Quit},*reply_);}catch(...){}}
#else
        if(process_.value&&WaitForSingleObject(process_.value,0)!=WAIT_OBJECT_0) {
            try {send({native::magic,native::Operation::Quit});}catch(...){}
            WaitForSingleObject(process_.value,2000); // job closes and terminates a stuck child
        }
#endif
    }
    CommandResult apply(EntityId actor,const Command& command) override {
        if(actor!=view_.localPlayer)return CommandResult::UnknownEntity;
        if(command.type==Command::Type::SetRun){running_=command.x!=0;return CommandResult::Accepted;}
        if(command.type==Command::Type::MoveTo) {
            GridPoint destination{command.x,command.y};
            if(!grid_.walkable(destination))return CommandResult::Rejected;
            held_={};attack_.reset();pickup_.reset();interact_.reset();destination_.reset();target_.reset();
            const auto here=subtileOf(view_.player().position);
            if(std::max(std::abs(destination.x-here.x),std::abs(destination.y-here.y))>14){destination_=destination;destinationTicks_=0;}
            else target_=destination;
            return CommandResult::Accepted;
        }
        if(command.type==Command::Type::Hold) {
            held_={std::clamp(command.x,-1,1),std::clamp(command.y,-1,1)};if(held_!=GridPoint{}){attack_.reset();destination_.reset();}
            if(held_==GridPoint{})target_=subtileOf(view_.player().position);
            return CommandResult::Accepted;
        }
        if(command.type==Command::Type::Stop){destination_.reset();held_={};attack_.reset();target_=subtileOf(view_.player().position);return CommandResult::Accepted;}
        if(command.type==Command::Type::Pickup) {
            for(const auto& item:view_.items)if(item.id==uint32_t(command.x)&&(item.mode==3||item.mode==5)){pickup_=command.x;attack_.reset();target_.reset();destination_.reset();return CommandResult::Accepted;}
            return CommandResult::Rejected;
        }
        if(command.type==Command::Type::Interact) {
            const EntityId id=1000+EntityId(command.x);
            for(const auto& e:view_.entities)if(e.id==id&&e.kind==EntityKind::TownPerson){interact_=command.x;attack_.reset();pickup_.reset();interactObject_.reset();target_.reset();destination_.reset();return CommandResult::Accepted;}
            return CommandResult::Rejected;
        }
        if(command.type==Command::Type::InteractObject) {
            for(const auto* list:{&view_.objects,&view_.tiles})for(const auto& o:*list)if(int32_t(o.id)==command.x){interactObject_=command.x;attack_.reset();pickup_.reset();interact_.reset();target_.reset();destination_.reset();return CommandResult::Accepted;}
            return CommandResult::Rejected;
        }
        if(command.type==Command::Type::Respawn){if(view_.vitals.life>0)return CommandResult::Rejected;respawn_=true;return CommandResult::Accepted;}
        if(command.type==Command::Type::Cheat) {
            native::Request r;r.operation=native::Operation::Cheat;r.x=command.x;r.y=command.y;r.z=command.z;
            packets_.push_back({r,false});
            return CommandResult::Accepted;
        }
        {
            // Inventory commands are client packets: 0x19 take, 0x18 place, 0x1A equip, 0x1D swap, 0x1C take body, 0x20 use, 0x26 use belt.
            uint32_t header=0,size=0;int32_t a=command.x,b=0,c=0,d=0;
            switch(command.type) {
            case Command::Type::TakeItem:header=0x19;size=5;break;
            case Command::Type::PlaceItem:header=0x18;size=17;b=command.y;c=command.z;d=command.w;break;
            case Command::Type::EquipItem:header=0x1A;size=9;b=command.y;break;
            case Command::Type::SwapBody:header=0x1D;size=9;b=command.y;break;
            case Command::Type::TakeBody:header=0x1C;size=3;break;
            case Command::Type::UseItem:header=0x20;size=13;break;
            case Command::Type::UseBelt:header=0x26;size=13;break;
            case Command::Type::DropItem:header=0x17;size=5;break;
            case Command::Type::DropGold:header=0x50;size=9;a=int32_t(guid_);b=command.x;break; // the player's own number, then the amount
            case Command::Type::CastAt:{header=command.z?0x0C:0x05;size=5;const int32_t px=command.x+origin_.x,py=command.y+origin_.y;a=(px&0xFFFF)|(py<<16);break;}
            case Command::Type::CastOn:header=command.z?0x0D:0x06;size=9;a=1;b=command.x;break;
            case Command::Type::Raw:header=uint32_t(command.x)&255;size=(uint32_t(command.x)>>8)&255;a=command.y;b=command.z;c=command.w;d=command.v;break;
            default:break;
            }
            if(header){native::Request r;r.operation=native::Operation::Packet;r.x=int32_t(header|(size<<8));r.y=a;r.z=b;r.w=c;r.v=d;packets_.push_back({r,command.type==Command::Type::UseItem});attack_.reset();pickup_.reset();
                // A skill used on a spot or a unit (0x05-0x10) replaces a walk in progress: the long-walk hops sent every 10
                // ticks would otherwise cut the skill off (a Dragon Flight cast while walking became a walk 3 ticks later).
                if(header>=0x05&&header<=0x10){destination_.reset();target_.reset();}
                return CommandResult::Accepted;}
        }
        if(command.type==Command::Type::Attack) {
            const EntityId id=1000+EntityId(command.x);
            for(const auto& e:view_.entities)if(e.id==id&&e.kind==EntityKind::Enemy){attack_=command.x;attackNow_=true;held_={};target_.reset();destination_.reset();return CommandResult::Accepted;}
            return CommandResult::Rejected;
        }
        return CommandResult::Rejected;
    }
    void tick() override {
        if(held_!=GridPoint{}) {
            auto here=subtileOf(view_.player().position);auto next=here;
            for(int i=0;i<3;++i){GridPoint p{next.x+held_.x,next.y+held_.y};if(!grid_.canStep(next,p))break;next=p;}
            target_=next;
        }
        native::Request request;
        if(attack_) {
            const EntityId id=1000+EntityId(*attack_);bool alive=false;
            for(const auto& e:view_.entities)alive|=e.id==id&&e.kind==EntityKind::Enemy;
            if(!alive)attack_.reset();
        }
        if(!packets_.empty()) {
            const auto packet=packets_.front();packets_.pop_front();request=packet.request;
            if(packet.usePlayerPosition) {
                // Original PlrMsg.cpp Rcv0x20_UseGridItem validates world X/Y
                // within 50 subtiles. Resolve at dispatch so queued uses follow
                // movement; raw packets retain their explicitly supplied target.
                const auto here=subtileOf(view_.player().position);
                request.z=here.x+origin_.x;request.w=here.y+origin_.y;
            }
        }
        else if(respawn_){request.operation=native::Operation::Respawn;respawn_=false;attack_.reset();target_.reset();}
        else if(interact_) {
            // Walk up to the person (the server only starts a conversation within a few subtiles), then talk.
            const EntityId id=1000+EntityId(*interact_);const EntitySnapshot* person=nullptr;
            for(const auto& e:view_.entities)if(e.id==id)person=&e;
            if(!person)interact_.reset();
            else {
                const auto here=subtileOf(view_.player().position),there=subtileOf(person->position);
                const int dx=there.x-here.x,dy=there.y-here.y;
                if(dx*dx+dy*dy<=16){request.operation=native::Operation::Packet;request.x=int32_t(0x13|(9<<8));request.y=1;request.z=*interact_;interact_.reset();}
                else if(view_.tick%15==0){const auto hop=hopToward(here,there);request.operation=native::Operation::Walk;request.x=hop.x+origin_.x;request.y=hop.y+origin_.y;}
            }
        }
        else if(interactObject_) {
            // Walk up to the object, then open/use it (packet 0x13, unit type 2 = UNIT_OBJECT). The server itself
            // finishes the approach if it is still a little out of range, same as the original client.
            const ObjectSnapshot* object=nullptr;
            for(const auto* list:{&view_.objects,&view_.tiles})for(const auto& candidate:*list)if(int32_t(candidate.id)==*interactObject_)object=&candidate;
            const bool isTile=(uint32_t(*interactObject_)&tileIdBit)!=0;
            if(!object)interactObject_.reset();
            else {
                const auto here=subtileOf(view_.player().position);
                const int dx=object->x-here.x,dy=object->y-here.y;
                if(dx*dx+dy*dy<=(isTile?100:25)){request.operation=native::Operation::Packet;request.x=int32_t(0x13|(9<<8));request.y=isTile?5:2;request.z=int32_t(uint32_t(*interactObject_)&~tileIdBit);interactObject_.reset();}
                else if(view_.tick%15==0){const auto hop=hopToward(here,{object->x,object->y});request.operation=native::Operation::Walk;request.x=hop.x+origin_.x;request.y=hop.y+origin_.y;}
            }
        }
        else if(pickup_) {
            // Walk up to the item (the server refuses distant pickups), then pick it up.
            const ItemSnapshot* item=nullptr;
            for(const auto& candidate:view_.items)if(candidate.id==uint32_t(*pickup_)&&(candidate.mode==3||candidate.mode==5))item=&candidate;
            if(!item)pickup_.reset();
            else {
                const auto here=subtileOf(view_.player().position);
                const int dx=item->x-here.x,dy=item->y-here.y;
                if(dx*dx+dy*dy<=9){request.operation=native::Operation::Pickup;request.x=*pickup_;pickup_.reset();}
                else if(view_.tick%15==0){const auto hop=hopToward(here,{item->x,item->y});request.operation=native::Operation::Walk;request.x=hop.x+origin_.x;request.y=hop.y+origin_.y;}
            }
        }
        else if(attack_&&(attackNow_||view_.tick%10==0)){request.operation=native::Operation::Attack;request.x=*attack_;attackNow_=false;}
        else if(destination_) {
            const auto here=subtileOf(view_.player().position);
            if(std::max(std::abs(destination_->x-here.x),std::abs(destination_->y-here.y))<=1||++destinationTicks_>750)destination_.reset();
            else if(view_.tick%10==0){const auto hop=hopToward(here,*destination_);request.operation=native::Operation::Walk;request.x=hop.x+origin_.x;request.y=hop.y+origin_.y;}
        }
        else if(target_){request.operation=native::Operation::Walk;request.x=target_->x+origin_.x;request.y=target_->y+origin_.y;target_.reset();}
        // Original client packet 0x03 requests run; D2Game owns speed/stamina.
        if(request.operation==native::Operation::Walk)request.z=running_?1:0;
        // Where the player is going, faced while moving: the walk target; for an attack or a skill on a unit, the unit (the
        // server walks the player up to it, and it may move); for a skill at a spot, the spot; for a pickup, the item.
        auto unitAt=[&](int32_t guid)->std::optional<GridPoint>{for(const auto& e:view_.entities)if(e.id==1000+EntityId(guid))return subtileOf(e.position);return std::nullopt;};
        const uint32_t header=uint32_t(request.x)&255;
        if(request.operation==native::Operation::Walk)walkGoal_=GridPoint{request.x-origin_.x,request.y-origin_.y};
        else if(request.operation==native::Operation::Attack){if(auto at=unitAt(request.x))walkGoal_=at;}
        else if(request.operation==native::Operation::Pickup){for(const auto& item:view_.items)if(item.id==uint32_t(request.x))walkGoal_=GridPoint{item.x,item.y};}
        else if(request.operation==native::Operation::Packet&&(header==0x06||header==0x0D)&&request.y==1){if(auto at=unitAt(request.z))walkGoal_=at;}
        else if(request.operation==native::Operation::Packet&&(header==0x05||header==0x0C))walkGoal_=GridPoint{(request.y&0xFFFF)-origin_.x,int32_t(uint32_t(request.y)>>16)-origin_.y};
        else if(attack_){if(auto at=unitAt(*attack_))walkGoal_=at;} // still after the enemy between attack requests
#ifdef D2MODERN_INPROCESS
        if(lanGuest_) {
            // What this player's character is shown walking toward before the host has moved it: a walk's final goal
            // (a far click's destination, else the hop); an attack, a skill or anything else ends it.
            if(request.operation==native::Operation::Walk){shownGoal_=destination_?*destination_:GridPoint{request.x-origin_.x,request.y-origin_.y};if(!view_.player().moving&&walkAsked_<0)walkAsked_=0;}
            else if(request.operation!=native::Operation::Tick)shownGoal_.reset();
            lanGuest_->send(request);playLanSnapshots();updateShown();return;
        }
#endif
        send(request);publish(receive(5000),false);
    }
#ifdef D2MODERN_INPROCESS
    // A LAN guest plays the host's snapshots one a tick from what has arrived, never waiting for the network: over
    // Wi-Fi (phones most of all) they come in bursts, and waiting for each one froze the game until the next burst.
    // A tick with nothing to play holds still, which leaves one more snapshot waiting from then on: the delay grows by
    // itself to what the bursts need. What never gets used is trimmed: when, over 2 seconds, there were always at
    // least n waiting, n-1 of them are played at once (the guest's clock running slow, a pause, a burst that has passed),
    // so the guest stays as close to the host's present as its Wi-Fi allows.
    size_t lanLeast_=SIZE_MAX;int lanWindow_=0;
    struct {uint64_t ticks=0,dry=0,extra=0;size_t most=0;} lanStats_; // reported every 10 seconds
    void playLanSnapshots() {
        lanGuest_->poll();
        // Scripted runs (smoke tests) step the game as fast as they can: there each tick waits for the host's next snapshot.
        static const bool waitForHost=[]{const char* v=std::getenv("D2MODERN_LAN_WAIT");return v&&*v;}();
        if(waitForHost&&!lanGuest_->buffered()){native::Reply next;lanGuest_->receive(next);publish(next,false);return;}
        size_t waiting=lanGuest_->buffered();
        ++lanStats_.ticks;lanStats_.most=std::max(lanStats_.most,waiting);
        lanLeast_=std::min(lanLeast_,waiting);
        size_t play=waiting?1:0;
        if(++lanWindow_>=50) {
            if(lanLeast_!=SIZE_MAX&&lanLeast_>=2)play+=std::min(lanLeast_-1,waiting-play);
            lanLeast_=SIZE_MAX;lanWindow_=0;
        }
        if(waiting>50)play=waiting-1; // 2 seconds behind (back from a pause): straight to the present
        if(!play) {
            if(lanGuest_->quietSeconds()>4)throw ConnectionLost("The connection to the host was lost");
            // Nothing for this tick: everything holds still (drawn where it is, not moved again over the last step).
            for(auto& e:view_.entities)e.previous=e.position;
            ++lanStats_.dry;
        }
        for(size_t i=0;i<play;++i){lanGuest_->next(*reply_);publish(*reply_,false);}
        if(play>1)lanStats_.extra+=play-1;
        if(lanStats_.ticks%250==0) {
            // In the output (D2Next.log on a phone): ticks with nothing to play, snapshots played through faster, the
            // most that were waiting (each one waiting is 40 ms behind the host), and how many are waiting now.
            std::printf("LAN: last 10 s: %llu ticks waited for the host, %llu snapshots caught up, at most %zu waiting, %zu now\n",
                (unsigned long long)lanStats_.dry,(unsigned long long)lanStats_.extra,lanStats_.most,lanGuest_->buffered());std::fflush(stdout);
            lanStats_={lanStats_.ticks,0,0,0};
        }
    }
    // A LAN guest's own character is shown moving the moment a walk is asked for, as the original client moved it
    // itself before the server answered: without that every click waited a round trip to the host. Only what is shown
    // moves ahead; requests, pathing and everything else use where the host says the character is. The character
    // walks toward the goal at the speed the host has been moving it (measured, walking and running apart: the first
    // walk after joining is not shown ahead), at most as far ahead as the host's answers take (measured from a walk
    // asked for to the host moving it), on walkable ground; then it is drawn back to the host's position, and put
    // there at once when the two disagree by more (a wall the host walked around, a teleport, a new level).
    FixedPoint shown_{},shownBefore_{};bool shownValid_=false,shownMoving_=false;uint32_t shownTicks_=0;
    std::optional<GridPoint> shownGoal_;
    int walkAsked_=-1;              // ticks since a walk was asked for while the host had the character standing (-1 none)
    int hostStanding_=0;            // ticks the host has had the character standing
    double walkStep_=0,runStep_=0;  // subtiles a tick the host moves the character walking and running
    double answerTicks_=3;          // ticks from a walk asked for to the host moving the character
    static bool townLevel(uint32_t level){return level==1||level==40||level==75||level==103||level==109;}
    void updateShown() {
        const auto& p=view_.entities[0];
        const double sx=double(p.position.x)/fixedOne,sy=double(p.position.y)/fixedOne;
        if(p.moving) {
            const double dx=double(p.position.x-p.previous.x)/fixedOne,dy=double(p.position.y-p.previous.y)/fixedOne,step=std::sqrt(dx*dx+dy*dy);
            if(step>0.05&&step<2&&(p.mode==2||p.mode==3||p.mode==6)){auto& slot=p.mode==3?runStep_:walkStep_;slot=slot>0?slot*.8+step*.2:step;}
            if(walkAsked_>=0){answerTicks_=std::clamp(answerTicks_*.7+walkAsked_*.3,1.0,12.0);walkAsked_=-1;}
        } else if(walkAsked_>=0&&++walkAsked_>50)walkAsked_=-1;
        // A goal the host has stopped short of (blocked, or the walk was cut off): the character stays where the host has it.
        hostStanding_=p.moving?0:hostStanding_+1;
        if(shownGoal_&&walkAsked_<0&&hostStanding_>int(answerTicks_)+4)shownGoal_.reset();
        if(!shownValid_){shown_=p.position;shownValid_=true;}
        shownBefore_=shown_;
        const double speed=running_&&!townLevel(view_.level)&&runStep_>0?runStep_:walkStep_;
        double x=double(shown_.x)/fixedOne,y=double(shown_.y)/fixedOne;
        if(shownGoal_&&speed>0) {
            const double gx=shownGoal_->x+.5,gy=shownGoal_->y+.5,dx=gx-x,dy=gy-y,distance=std::sqrt(dx*dx+dy*dy);
            const double nx=distance<=speed?gx:x+dx/distance*speed,ny=distance<=speed?gy:y+dy/distance*speed;
            if(grid_.walkable(GridPoint{int(std::floor(nx)),int(std::floor(ny))})){x=nx;y=ny;}
            if(std::abs(sx-gx)<1&&std::abs(sy-gy)<1&&!p.moving)shownGoal_.reset();
        }
        // No further ahead of the host than its answers take; drawn back (or put) to where the host has it.
        const double lead=shownGoal_?(answerTicks_+1)*speed:0,dx=x-sx,dy=y-sy,apart=std::sqrt(dx*dx+dy*dy);
        if(apart>lead+4){x=sx;y=sy;}
        else if(apart>lead){const double back=std::min(apart-lead,std::max(speed>0?speed:.5,(apart-lead)*.5));x-=dx/apart*back;y-=dy/apart*back;}
        shown_={int32_t(std::lround(x*fixedOne)),int32_t(std::lround(y*fixedOne))};
        shownMoving_=!(shown_==shownBefore_);
        shownTicks_=shownMoving_?shownTicks_+1:0;
        static const bool log=std::getenv("D2MODERN_SHOWN_LOG")!=nullptr; // test hook: the shown position against the host's, every tick
        if(log)std::printf("Shown: tick %u host %.2f,%.2f shown %.2f,%.2f%s ahead %.2f (walk %.3f run %.3f, answers in %.1f ticks)\n",unsigned(view_.tick),sx,sy,
            double(shown_.x)/fixedOne,double(shown_.y)/fixedOne,shownGoal_?" walking":"",std::hypot(double(shown_.x)/fixedOne-sx,double(shown_.y)/fixedOne-sy),walkStep_,runStep_,answerTicks_);
    }
    WorldSnapshot shownView() const {
        WorldSnapshot shown=view_;auto& p=shown.entities[0];
        const bool hostMoving=p.moving;
        p.position=shown_;p.previous=shownBefore_;p.moving=shownMoving_;
        if(shownMoving_){const double dx=double(shown_.x-shownBefore_.x),dy=double(shown_.y-shownBefore_.y);if(dx*dx+dy*dy>0)p.facing=uint8_t(facingToward(dx,dy));}
        const bool sequence=p.mode==18||p.skillMoveFrame!=255;
        if(!sequence&&shownMoving_&&!hostMoving){p.mode=uint8_t(townLevel(view_.level)?6:running_?3:2);p.animationFrame=shownTicks_;} // walking before the host has started
        else if(!sequence&&!shownMoving_&&hostMoving&&!shownGoal_){p.mode=uint8_t(townLevel(view_.level)?5:1);p.animationFrame=shownTicks_;} // arrived before the host has
        return shown;
    }
#endif
    WorldSnapshot snapshot() const override {
#ifdef D2MODERN_INPROCESS
        if(lanGuest_&&shownValid_&&!view_.entities.empty())return shownView();
#endif
        return view_;
    }
};
}
bool nativeGameLinkedIn() {
#ifdef D2MODERN_INPROCESS
    return true;
#else
    return false;
#endif
}
#ifdef D2MODERN_INPROCESS
#ifdef __EMSCRIPTEN__
bool lanAvailable(){return false;} // browsers have no raw TCP; a WebSocket transport is later work (LAN.md)
#else
bool lanAvailable(){return true;}
#endif
std::string lanLocalAddress(){return lan::localAddress();}
bool lanValidAddress(const std::string& address){return lan::validAddress(address);}
#else
bool lanAvailable(){return false;}
std::string lanLocalAddress(){return {};}
bool lanValidAddress(const std::string&){return false;}
#endif
CharacterAppearance appearanceFromCodes(uint32_t characterClass,const std::array<uint32_t,17>& codes) {
    constexpr const char* tokens[]={"am","so","ne","pa","ba","dz","ai"};
    if(characterClass>=7)throw std::runtime_error("Unknown native character class");
    auto token=[](uint32_t code) {
        std::string result;
        if(!code)return result;
        for(int i=0;i<3;++i) {
            char c=char((code>>(i*8))&255);if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')))throw std::runtime_error("Invalid native appearance token");
            result+=c;
        }
        return result;
    };
    CharacterAppearance appearance;
    appearance.token=tokens[characterClass];
    appearance.weaponClass=token(codes[0]);
    for(size_t i=0;i<16;++i) {
        appearance.components[i]=token(codes[i+1]&0xffffff);
        if(const uint32_t packed=codes[i+1]>>24){appearance.trans[i]=uint8_t((packed-1)/21);appearance.colors[i]=uint8_t((packed-1)%21);}
    }
    return appearance;
}
NativeGameStart startNativeGame(const std::filesystem::path& dlls,const std::filesystem::path& game,const std::filesystem::path& save,uint32_t seed,const CollisionGrid& collision,GridPoint origin,const std::vector<int>& levels,const std::filesystem::path& saveOut,const std::function<void()>& waiting) {
    auto backend=std::make_unique<NativeGame>(dlls,game,save,seed,collision,origin,levels,saveOut,waiting);
    NativeGameStart result;result.seed=backend->seed;result.characterClass=backend->characterClass;
    result.appearance=backend->appearance;result.coloredEquipment=backend->coloredEquipment;
    result.backend=std::move(backend);return result;
}
}
