// The world as the renderer draws it: map tiles in painter order, units, shadows, overlays, light and the automap's
// counterpart on the ground.
#include <set>
#include "Renderer.h"
#include "SkillVisuals.h"
#include "NativeGame.h"

const d2::Animation* Renderer::tintedPlayer(uint8_t mode,const d2::Animation& untinted,uint8_t shift) {
    if(!shift||mode>=20||!stateColors||!unitPalette||!archives||!playerLook)return &untinted;
    const auto* map=stateColors->shift(shift);if(!map)return &untinted;
    const auto key=std::make_pair(mode,shift);
    if(const auto found=playerTints.find(key);found!=playerTints.end())return found->second?found->second.get():&untinted;
    // Same composition as UnitArt: the Pal.PL2 map remaps each palette index, then the act palette is read. The player has
    // no palshift. DCC index 0 is skipped before the lookup (decodeDcc), so a map entry of 0 is a colour, not a hole.
    d2::Palette palette=*unitPalette;
    for(size_t i=0;i<256;++i)palette[i]=(*unitPalette)[(*map)[i]];
    auto look=*playerLook;
    if(mode==0||mode==17)look.weaponClass="hth";
    std::unique_ptr<d2::Animation> decoded;
    try {decoded=std::make_unique<d2::Animation>(d2::loadCharacter(*archives,palette,d2::playerModeTokens[mode],look));}
    catch(const std::exception&){}
    const auto* stored=playerTints.emplace(key,std::move(decoded)).first->second.get();
    return stored?stored:&untinted;
}
bool Renderer::draw() {
        if(!begin({0,0,0,1}))return false; // beyond the level is black, as in the game
        int drawBlend=0; // for the next pictures: 0 solid, 1 added onto the scene (glow), 2 see-through by blendFactor
        float blendFactor=.5f;
        // Each picture of the world becomes a draw command (DrawList.h): its quad in clip space, the current light and blend.
        commands.clear();
        auto drawTile=[&](uint32_t w,uint32_t h,float centerX,float centerY,const d2::Image* image,bool shadow) {
            d2::DrawCommand c;c.image=image;
            c.quad={w*zoom/width,h*zoom/height,2*centerX/width,-2*centerY/height};
            c.light=currentLight;
            c.blend=shadow?d2::DrawCommand::Blend::Shadow:drawBlend==1?d2::DrawCommand::Blend::Add:drawBlend==2?d2::DrawCommand::Blend::Mix:d2::DrawCommand::Blend::Normal;
            c.amount=blendFactor;
            commands.push_back(c);
        };
        auto setOverlayBlend=[&](int trans) {
            drawBlend=trans==3||trans==6?1:2;
            blendFactor=trans==0?.75f:trans==2?.25f:.5f;
        };
        auto setMissileBlend=[&](int trans) {
            if(trans==1||trans==3||trans==6){drawBlend=1;blendFactor=1.f;}
            else if(trans==2){drawBlend=2;blendFactor=.5f;}
            else {drawBlend=0;blendFactor=1.f;}
        };
        if(scene) {
            const float left=scene->width*.5f-(width*.5f+panX)/zoom;
            const float top=scene->height*.5f-(height*.5f+panY)/zoom;
            auto visible=d2::visibleTiles(*scene,left,top,left+width/zoom,top+height/zoom);
            visibleCount=visible.size();
            // The light sources of this frame, and the light of a picture: one value where it stands, or across a floor tile.
            lights.clear();ambient={1,1,1};
            const auto* levelInfo=levelTable?levelTable->byId(int32_t(worldView.level)):nullptr;
            // Outside: D2Client fills its light grid with the act's light (D2Common #10926 intensity, #10927 colour; 0x6FAA3E86).
            // 128 is the full light of the art (the Pandemonium Fortress's fixed intensity); more is not drawn brighter.
            if(levelInfo&&!levelInfo->inside) {
                const float level=std::min(1.f,float(worldView.envIntensity)/128.f);
                for(int k=0;k<3;++k)ambient[k]=worldView.envColor[size_t(k)]/255.f*level;
            } else ambient={insideAmbient,insideAmbient,insideAmbient};
            lightingOn=walkGrid&&levelInfo&&(levelInfo->inside||ambient[0]<1||ambient[1]<1||ambient[2]<1);
            if(lightingOn) {
                auto lightColor=[&](std::array<float,3> color,const std::array<uint32_t,6>& states) {
                    // states.txt light-r/g/b of the highest colorpri replaces the colour of the light this unit already gives.
                    if(stateTable)if(const auto look=stateTable->visual(states);look.light)return std::array<float,3>{look.lightColor[0]/255.f,look.lightColor[1]/255.f,look.lightColor[2]/255.f};
                    return color;
                };
                if(idleAnimation)lights.push_back({view.x,view.y,playerLightRadius,lightColor({1,1,1},worldView.player().states)});
                if(playerOverlay.animation&&playerOverlay.radius>0)lights.push_back({view.x,view.y,playerOverlay.radius,playerOverlay.color});
                for(const auto& l:clientLights)lights.push_back({l.x,l.y,l.radius,l.color});
                for(const auto& o:others) {
                    if(o.kind==5&&objectTable)if(const auto* info=objectTable->find(o.classId);info&&o.mode<8&&info->lit[o.mode])
                        lights.push_back({o.x,o.y,float(info->lit[o.mode]),{info->lightColor[0]/255.f,info->lightColor[1]/255.f,info->lightColor[2]/255.f}});
                    // Monsters that glow (monstats2.txt Light), while alive.
                    if((o.kind==1||o.kind==2)&&unitArt)if(const auto* sprite=unitArt->sprite(o.classId);sprite&&sprite->light>0)
                        lights.push_back({o.x,o.y,float(sprite->light),lightColor({sprite->lightColor[0]/255.f,sprite->lightColor[1]/255.f,sprite->lightColor[2]/255.f},o.states)});
                    if(o.kind==4&&missileTable)if(const auto* info=missileTable->find(o.classId);info&&info->light>0) {
                        // pCltDoFunc 11 holds the last frame and drops the light once the animation has reached it.
                        bool held=false;
                        if(info->clientDo==11&&info->animLen>1) {
                            const unsigned speed=std::max<unsigned>(o.speed,unsigned(std::max(1,info->animRate/4)));
                            held=unsigned(o.ticks)*speed/256+1>=unsigned(info->animLen);
                        }
                        if(!held)lights.push_back({o.x,o.y,float(info->light),{info->lightColor[0]/255.f,info->lightColor[1]/255.f,info->lightColor[2]/255.f}});
                    }
                }
            }
            auto lightStanding=[&](float x,float y){if(!lightingOn)return;const auto l=lightAt(x,y);setLight(l,l,l,l);};
            // A frame's see-through layers, over it at the same place: glowing (added on) or 75/50/25% opaque, by the COF effect.
            auto drawGlow=[&](const d2::AnimationFrame& frame,float centerX,float centerY) {
                if(frame.glowEffect==255||frame.glow.rgba.empty())return;
                const int saved=drawBlend;const float savedFactor=blendFactor;
                const auto e=frame.glowEffect;
                setOverlayBlend(e);

                drawTile(frame.glow.width,frame.glow.height,centerX,centerY,&frame.glow,false);
                drawBlend=saved;blendFactor=savedFactor;
            };
            auto groundOf=[&](float px,float py){const float u=(px-float(scene->originX))/16,v=(py-float(scene->originY))/8;return std::array<float,2>{(u+v)*.5f,(v-u)*.5f};};
            // A unit's shadow darkens only the ground drawn so far (the shadow pass runs after the floors, before walls and units).
            // The player's picture this frame (scene pixels: left, top, width, height), for fading the walls in front of it.
            std::array<float,4> playerBox{};const d2::AnimationFrame* playerFrame=nullptr;
            constexpr float wallFade=.38f; // how much of a wall in front of the player is drawn (the rest shows what is behind)
            auto drawShadow=[&](const d2::AnimationFrame& frame,const std::array<float,2>& anchor) {
                if(const auto* s=shadowOf(frame))drawTile(s->image.width,s->image.height,(anchor[0]+s->x+s->image.width*.5f-scene->width*.5f)*zoom+panX,
                    (anchor[1]+s->y+s->image.height*.5f-scene->height*.5f)*zoom+panY,&s->image,true);
            };
            // The overlays of a unit's states (auras, Frozen Armor, Energy Shield, curses...), looping, at its feet: states.txt
            // overlay1-4 -> overlay.txt (file, frames, AnimRate, Trans, PreDraw: behind the unit). behind picks which to draw.
            auto drawOverlayFrame=[&](const d2::AnimationFrame& o,const d2::OverlayInfo& info,const std::array<float,2>& anchor,int overlayHeight) {
                const int h=std::clamp(overlayHeight,1,4)-1;
                drawTile(o.image.width,o.image.height,(anchor[0]+info.x+o.x+o.image.width*.5f-scene->width*.5f)*zoom+panX,
                    (anchor[1]+info.y+info.height[size_t(h)]+o.y+o.image.height*.5f-scene->height*.5f)*zoom+panY,&o.image,false);
            };
            auto drawStateOverlays=[&](const std::array<uint32_t,6>& states,const std::array<float,2>& anchor,bool behind,int overlayHeight) {
                if(!stateTable||!overlayTable||!overlayArt)return;
                for(uint32_t s=0;s<192;++s) {
                    if(!(states[s>>5]&(1u<<(s&31))))continue;
                    const auto* state=stateTable->find(s);if(!state)continue;
                    for(const auto& name:state->overlays) {
                        if(name.empty())continue;
                        const auto* info=overlayTable->find(name);if(!info||info->preDraw!=behind)continue;
                        const auto* overlay=overlayArt->get(name);if(!overlay||overlay->frames.empty())continue;
                        const int frame=info->loopFrame(worldView.tick,unsigned(overlay->frames.size()));if(frame<0)continue;
                        const auto& o=overlay->frames[size_t(frame)];
                        const int saved=drawBlend;const float savedFactor=blendFactor;
                        setOverlayBlend(info->trans);
                        drawOverlayFrame(o,*info,anchor,overlayHeight);
                        drawBlend=saved;blendFactor=savedFactor;
                    }
                }
            };
            auto drawOneShotOverlays=[&](uint32_t unit,const std::array<float,2>& anchor,bool behind,int overlayHeight) {
                if(!overlayTable||!overlayArt)return;
                for(const auto& once:unitOverlays) {
                    if(once.unit!=unit)continue;
                    const auto* info=overlayTable->find(once.name);const auto* overlay=overlayArt->get(once.name);
                    if(!info||info->preDraw!=behind||!overlay||overlay->frames.empty())continue;
                    const size_t frameIndex=std::min<size_t>(size_t((worldView.tick-once.start)*uint64_t(info->rate)/16),overlay->frames.size()-1);
                    const auto& o=overlay->frames[frameIndex];
                    const int saved=drawBlend;const float savedFactor=blendFactor;
                    setOverlayBlend(info->trans);
                    drawOverlayFrame(o,*info,anchor,overlayHeight);
                    drawBlend=saved;blendFactor=savedFactor;
                }
            };
            auto drawLoopingOverlay=[&](const std::string& name,uint64_t ticks,const std::array<float,2>& anchor,bool behind,int overlayHeight) {
                if(name.empty()||!overlayTable||!overlayArt)return;
                const auto* info=overlayTable->find(name);const auto* overlay=overlayArt->get(name);
                if(!info||info->preDraw!=behind||!overlay||overlay->frames.empty())return;
                const int frame=info->loopFrame(ticks,unsigned(overlay->frames.size()));if(frame<0)return;
                const auto& o=overlay->frames[size_t(frame)];
                const int saved=drawBlend;const float savedFactor=blendFactor;
                setOverlayBlend(info->trans);
                drawOverlayFrame(o,*info,anchor,overlayHeight);
                drawBlend=saved;blendFactor=savedFactor;
            };
            auto drawCharacter=[&](bool shadowOnly=false) {
                if(!idleAnimation||!walkGrid)return;
                // The sequence mode (18: Charge, Whirlwind, Zeal, Leap...) draws the mode and frame the game's sequence table names
                // for the step the game is on (sent with the snapshot). Without that, the mode its skill names (skills.txt seqtrans).
                uint8_t mode=view.mode;
                const bool skillSequence=mode==18||view.skillMoveFrame!=255;
                if(skillSequence&&skillTable)if(const auto* skill=skillTable->find(uint32_t(castSkill));skill&&skill->seqMode>=0)mode=uint8_t(skill->seqMode);
                if(skillSequence)if(const int leap=d2::leapSequenceMode(castSkill,view.skillMoveFrame!=255?view.skillMoveFrame:view.tick);leap>=0)mode=uint8_t(leap);
                const bool fromTable=view.skillMoveFrame==255&&view.mode==18&&view.sequenceMode!=255; // Leap's flight and landing excepted
                if(fromTable)mode=view.sequenceMode;
                const auto found=movementAnimations.find(mode);
                const auto& source=found!=movementAnimations.end()?*found->second:(view.moving?*walkAnimation:*idleAnimation);
                const uint8_t artMode=found!=movementAnimations.end()?mode:uint8_t(view.moving?6:5);
                const auto* animation=tintedPlayer(artMode,source,stateTable?stateTable->visual(worldView.player().states).shift:0);
                // Shapeshifted (Werewolf, Werebear): the form's monster art in the mode the game converted the player's to.
                if(view.shapeClass!=0xFFFF&&unitArt)if(const auto* form=unitArt->get(view.shapeClass,view.shapeMode,0u);form&&!form->frames.empty())animation=form;
                if(!animation||animation->frames.empty()||animation->framesPerDirection==0)return;

                const bool unarmed=playerLook&&playerLook->weaponClass=="hth";
                const uint64_t sequenceFrame=fromTable?view.sequenceFrame:view.skillMoveFrame!=255?d2::playerSequenceFrame(castSkill,view.skillMoveFrame,unarmed):view.mode==18?d2::playerSequenceFrame(castSkill,view.tick,unarmed):view.tick;
                const unsigned dirs=std::max(1u,animation->directions),fpd=animation->framesPerDirection;
                const size_t index=(view.facing%16*dirs/16)*fpd+sequenceFrame%fpd;
                if(index>=animation->frames.size())return;
                const auto& frame=animation->frames[index];const auto groundAnchor=characterAnchor(*scene,view);
                const auto anchor=groundAnchor;
                playerBox={std::round(anchor[0])+frame.x,std::round(anchor[1])+frame.y,float(frame.image.width),float(frame.image.height)};playerFrame=&frame;
                if(shadowOnly){drawShadow(frame,groundAnchor);return;}
                lightStanding(view.x,view.y);
                const float cx=(anchor[0]+frame.x+frame.image.width*.5f-scene->width*.5f)*zoom+panX,cy=(anchor[1]+frame.y+frame.image.height*.5f-scene->height*.5f)*zoom+panY;
                auto drawOverlay=[&] {
                    // The cast glow, anchored at the character's feet (its frames carry their own offsets).
                    const auto* overlay=playerOverlay.animation;if(!overlay||overlay->frames.empty())return;
                    const auto& o=overlay->frames[std::min<size_t>(playerOverlay.frame,overlay->frames.size()-1)];
                    const int saved=drawBlend;const float savedFactor=blendFactor;
                    setOverlayBlend(playerOverlay.trans);

                    drawTile(o.image.width,o.image.height,(anchor[0]+playerOverlay.x+o.x+o.image.width*.5f-scene->width*.5f)*zoom+panX,
                        (anchor[1]+playerOverlay.y+o.y+o.image.height*.5f-scene->height*.5f)*zoom+panY,&o.image,false);
                    drawBlend=saved;blendFactor=savedFactor;
                };
                const std::array<float,2> feet{anchor[0],anchor[1]};
                drawStateOverlays(worldView.player().states,feet,true,2);
                drawOneShotOverlays(0,feet,true,2);
                if(playerOverlay.behind)drawOverlay(); // overlay.txt PreDraw: behind the character
                // Whirlwind (skills.txt srvdofunc 76) in the sequence mode: D2Client 1.10f (0x6FB4BA32, 0x6FB4BB12 → 0x6FAC23F0)
                // also draws the unit turned 21/64 of a circle before it and 42/64 after it, the same frame each time, so with
                // the game holding A1 frame 3 the character shows three ways at once.
                const auto* usedSkill=skillTable&&view.mode==18?skillTable->find(uint32_t(castSkill)):nullptr;
                const bool whirlwind=usedSkill&&usedSkill->serverDoFunc==76;
                auto drawTurned=[&](unsigned sixtyFourths) {
                    const unsigned turned=((view.facing*4+sixtyFourths)&63)/4; // our facings are sixteenths of a circle
                    const size_t i=(turned*dirs/16)*fpd+sequenceFrame%fpd;if(i>=animation->frames.size())return;
                    const auto& f=animation->frames[i];
                    drawTile(f.image.width,f.image.height,(anchor[0]+f.x+f.image.width*.5f-scene->width*.5f)*zoom+panX,(anchor[1]+f.y+f.image.height*.5f-scene->height*.5f)*zoom+panY,&f.image,false);
                };
                // Charge (srvdofunc 67) while running (its sequence's RN steps, when the client's skill flag 1 is set): D2Client
                // (0x6FB4BA4B → 0x6FAC9620) draws earlier copies behind the unit. Their number is the skill's param3, which
                // Charge's start function sets to 0 and its do function (on every running step) raises to at most 2. Copy i
                // stands i steps back along the unit's screen velocity, a step being that velocity scaled to 15 pixels by its
                // approximate length (max + min/2), one animation frame earlier each (wrapping within 8). Moving down the
                // screen they are drawn before the unit, farthest first; otherwise after it, nearest first.
                const bool charging=usedSkill&&usedSkill->serverDoFunc==67&&fromTable&&view.sequenceMode==3;
                static uint64_t chargeSteps=0,chargeTick=0;chargeSteps=charging?chargeSteps+(worldView.tick!=chargeTick?1:0):0;chargeTick=worldView.tick;
                auto drawChargeTrail=[&](bool before) {
                    const int copies=int(std::min<uint64_t>(chargeSteps,2));
                    if(!charging||!copies)return;
                    const auto& pl=worldView.player();
                    const auto now=d2::projectPoint(*scene,float(pl.position.x)/d2::fixedOne,float(pl.position.y)/d2::fixedOne);
                    const auto then=d2::projectPoint(*scene,float(pl.previous.x)/d2::fixedOne,float(pl.previous.y)/d2::fixedOne);
                    const int vx=int(std::lround(now[0]-then[0])),vy=int(std::lround(now[1]-then[1]));
                    if(before!=(vy>=0))return;
                    const int ax=std::abs(vx),ay=std::abs(vy);
                    const int length=std::max(1,(ax>ay?ay+2*ax:ax+2*ay)/2);
                    const int stepX=vx*15/length,stepY=vy*15/length;
                    int frameNow=int(sequenceFrame%animation->framesPerDirection);
                    for(int i=1;i<=copies;++i) {
                        if(--frameNow<0)frameNow+=8;
                        const int back=before?copies+1-i:i;
                        const auto& f=animation->frames[(view.facing*animation->directions/16)*animation->framesPerDirection+unsigned(frameNow)%animation->framesPerDirection];
                        drawTile(f.image.width,f.image.height,(anchor[0]-float(back*stepX)+f.x+f.image.width*.5f-scene->width*.5f)*zoom+panX,
                            (anchor[1]-float(back*stepY)+f.y+f.image.height*.5f-scene->height*.5f)*zoom+panY,&f.image,false);
                    }
                };
                drawChargeTrail(true);
                if(whirlwind)drawTurned(21);
                drawTile(frame.image.width,frame.image.height,cx,cy,&frame.image,false);
                drawGlow(frame,cx,cy);
                if(whirlwind)drawTurned(42);
                drawChargeTrail(false);
                if(!playerOverlay.behind)drawOverlay();
                drawOneShotOverlays(0,feet,false,2);
                drawStateOverlays(worldView.player().states,feet,false,2);
            };
            // Monsters and NPCs: their animation frame from the ticks spent in the current mode, else a debug glyph.
            // Only units monstats2.txt lets the mouse select (isSel, not noSel) are named and clickable: the camp's wandering Rogues
            // (name "Dummy", "an evil force"), townsfolk and critters are not.
            auto pickable=[&](const OtherUnit& other){const auto* sprite=unitArt?unitArt->sprite(other.classId):nullptr;return !sprite||sprite->selectable;};
            auto drawOther=[&](const OtherUnit& other,bool shadowOnly=false) {
                const bool isObject=other.kind==5;
                const bool isMissile=other.kind==4;
                const auto* placement=isObject&&objectTable?objectTable->find(other.classId):nullptr;
                const auto* missileInfo=isMissile&&missileTable?missileTable->find(other.classId):nullptr;
                const auto anchor=[&]{
                    const auto q=d2::projectPoint(*scene,other.x,other.y);
                    const float ox=placement?float(placement->xOffset):missileInfo?float(missileInfo->xOffset):0.f;
                    const float oy=placement?float(placement->yOffset):missileInfo?float(missileInfo->yOffset+missileInfo->zOffset):0.f;
                    return std::array<float,2>{std::round(q[0])+ox,std::round(q[1])+oy};
                }();
                // states.txt hide: a redeemed corpse (state 99) is gone from view (Redemption's ghost rises in its place).
                if(!isObject&&!isMissile&&(other.states[99>>5]&(1u<<(99&31))))return;
                const uint8_t stateShift=!isObject&&!isMissile&&stateTable?stateTable->visual(other.states).shift:0;
                // Another player (LAN) using a skill's sequence (Charge, Leap, Whirlwind...): the mode and frame chosen as for
                // this player's own character in drawCharacter, from the skill it uses (sent with it) instead of castSkill.
                const bool otherPlayer=!isObject&&!isMissile&&other.kind==0&&other.lookClass!=255;
                const auto* otherSkill=otherPlayer&&skillTable&&other.usedSkill?skillTable->find(other.usedSkill):nullptr;
                int sequenceArt=-1;uint64_t sequenceFrame=0;bool fromTable=false;
                if(otherPlayer&&(other.mode==18||other.skillMoveFrame!=255)) {
                    const int32_t skill=int32_t(other.usedSkill);
                    int mode=other.lookMode;
                    if(otherSkill&&otherSkill->seqMode>=0)mode=otherSkill->seqMode;
                    if(const int leap=d2::leapSequenceMode(skill,other.skillMoveFrame!=255?other.skillMoveFrame:other.ticks);leap>=0)mode=leap;
                    fromTable=other.skillMoveFrame==255&&other.mode==18&&other.sequenceMode!=255;
                    if(fromTable)mode=other.sequenceMode;
                    const bool unarmed=other.look[0]==(uint32_t('h')|uint32_t('t')<<8|uint32_t('h')<<16);
                    sequenceFrame=fromTable?other.sequenceFrame:other.skillMoveFrame!=255?d2::playerSequenceFrame(skill,other.skillMoveFrame,unarmed):d2::playerSequenceFrame(skill,other.ticks,unarmed);
                    sequenceArt=mode;
                }
                const d2::Animation* animation=isObject?(objectArt?objectArt->get(other.classId,other.mode):nullptr):isMissile?(missileArt?missileArt->get(other.classId):nullptr):(unitArt&&other.lookClass==255?unitArt->get(other.classId,other.mode,other.components,other.colorTransform,stateShift):nullptr);
                // A monster drawn as a player (Valkyrie, Decoy, Shadow Warrior): that class's art in its gear (D2Common #11013).
                if(!isObject&&!isMissile&&other.lookClass!=255&&unitArt)if(const auto* look=unitArt->player(d2::appearanceFromCodes(other.lookClass,other.look),sequenceArt>=0?uint8_t(sequenceArt):other.lookMode);look&&!look->frames.empty())animation=look;
                // An opened door: its opened art is empty; the closed door art holds the open door as its last frame.
                const bool openDoor=isObject&&placement&&placement->door&&other.mode==2;
                if(openDoor&&objectArt)animation=objectArt->get(other.classId,0);
                if(!animation&&unitArt&&other.mode!=1&&!isObject&&!isMissile&&other.lookClass==255)animation=unitArt->get(other.classId,1,other.components,other.colorTransform,stateShift);
                if(!animation||animation->frames.empty()) {
                    if(isMissile&&missileInfo&&!shadowOnly) {
                        drawLoopingOverlay(missileInfo->progressOverlay,other.ticks,anchor,true,2);
                        drawLoopingOverlay(missileInfo->progressOverlay,other.ticks,anchor,false,2);
                    }
                    if(isObject||isMissile||shadowOnly)return; // objects/missiles without art are not drawn
                    const d2::Image* glyph=other.kind==1?&townPersonGlyph:other.kind==2?&enemyGlyph:&corpseGlyph;
                    if(std::getenv("D2MODERN_PICK_LOG")){static std::set<uint64_t> told;if(told.insert(uint64_t(other.classId)<<8|other.mode).second)std::cout<<"No art: class "<<other.classId<<" ("<<(unitArt&&unitArt->sprite(other.classId)?unitArt->sprite(other.classId)->id:std::string("?"))<<") mode "<<other.mode<<" kind "<<int(other.kind)<<" at "<<other.x<<","<<other.y<<"\n";}
                    if((other.kind==1||other.kind==2)&&pickable(other))unitPicks.push_back({anchor[0]-16,anchor[1]-8,32,16,other.guid,other.classId,other.kind,other.monsterType,anchor[1]});
                    lightStanding(other.x,other.y);
                    drawTile(32,16,(anchor[0]-scene->width*.5f)*zoom+panX,(anchor[1]-scene->height*.5f)*zoom+panY,glyph,false);
                    return;
                }
                const unsigned perDirection=animation->framesPerDirection;
                const auto* objectInfo=isObject&&objectTable?objectTable->find(other.classId):nullptr;
                const bool looping=isObject?(objectInfo&&other.mode<8&&objectInfo->cycle[other.mode]):isMissile?(missileInfo&&missileInfo->loop):(other.mode==1||other.mode==2||other.mode==15);
                // Objects use only the frames objects.txt gives the mode (FrameCnt), from its start frame: a closed door's art file
                // also holds an open-looking frame that the closed mode never shows.
                const unsigned startFrame=openDoor?perDirection-1:objectInfo&&other.mode<8?std::min(objectInfo->start[other.mode],perDirection-1):0;
                const unsigned availableFrames=std::max(1u,perDirection-startFrame);
                const unsigned usedFrames=openDoor?1:objectInfo&&other.mode<8&&objectInfo->frames[other.mode]?std::min(objectInfo->frames[other.mode],availableFrames):availableFrames;
                const uint64_t displayTicks=other.ticks+(missileInfo&&missileInfo->randStart?uint64_t((other.guid*1103515245u+12345u)%std::max(1u,usedFrames)):0);
                const uint16_t animationSpeed=uint16_t(std::max<int>(other.speed,missileInfo?std::max(1,missileInfo->animRate/4):0));
                const unsigned lastFrame=std::min(perDirection-1,startFrame+usedFrames-1);
                unsigned frameInDirection=unsigned(displayTicks*animationSpeed/256);
                frameInDirection=looping?startFrame+frameInDirection%usedFrames:std::min(startFrame+frameInDirection,lastFrame);
                if(missileInfo&&missileInfo->subLoop&&missileInfo->subStop>=missileInfo->subStart) {
                    // missiles.txt SubLoop: the frames up to SubStop once, then SubStart..SubStop over and over (Sanctuary 10-30).
                    const unsigned raw=unsigned(displayTicks*animationSpeed/256),first=std::min<unsigned>(unsigned(missileInfo->subStart),usedFrames-1),last=std::min<unsigned>(unsigned(missileInfo->subStop),usedFrames-1);
                    frameInDirection=raw<=last?raw:first+(raw-first)%(last-first+1);
                }
                if(missileInfo&&missileInfo->clientDo==11&&missileInfo->animLen>0) {
                    const unsigned raw=unsigned(displayTicks*animationSpeed/256);
                    frameInDirection=std::min(raw,std::min<unsigned>(unsigned(missileInfo->animLen)-1,usedFrames-1));
                }
                if(sequenceArt>=0)frameInDirection=unsigned(sequenceFrame%perDirection);
                const size_t index=isObject?size_t(frameInDirection):(other.facing*animation->directions/16)*perDirection+frameInDirection;
                const auto& frame=animation->frames[index];
                if(shadowOnly){if(!isMissile)drawShadow(frame,anchor);return;}
                lightStanding(other.x,other.y);
                // A glowing missile (missiles.txt Trans 1/3/6, added onto the scene) gives light rather than takes it: drawn at full strength.
                if(missileInfo&&(missileInfo->trans==1||missileInfo->trans==3||missileInfo->trans==6))currentLight={1,1,1,1,1,1,1,1,1,1,1,1};
                // Spells and other missiles marked in missiles.txt glow (added onto the scene) or are half see-through.
                if(missileInfo)setMissileBlend(missileInfo->trans);else drawBlend=0;
                if(isObject&&objectInfo&&other.mode<8&&objectInfo->selectable[other.mode])objectPicks.push_back({anchor[0]+frame.x,anchor[1]+frame.y,float(frame.image.width),float(frame.image.height),other.guid,anchor[1]});
                if(!isObject&&!isMissile&&(other.kind==1||other.kind==2)&&pickable(other))unitPicks.push_back({anchor[0]+frame.x,anchor[1]+frame.y,float(frame.image.width),float(frame.image.height),other.guid,other.classId,other.kind,other.monsterType,anchor[1]});
                const float cx=(anchor[0]+frame.x+frame.image.width*.5f-scene->width*.5f)*zoom+panX,cy=(anchor[1]+frame.y+frame.image.height*.5f-scene->height*.5f)*zoom+panY;
                const int overlayHeight=!isObject&&!isMissile&&unitArt&&unitArt->sprite(other.classId)?unitArt->sprite(other.classId)->overlayHeight:2;
                if(isMissile&&missileInfo)drawLoopingOverlay(missileInfo->progressOverlay,other.ticks,anchor,true,2);
                if(!isObject&&!isMissile){drawBlend=0;drawStateOverlays(other.states,anchor,true,overlayHeight);drawOneShotOverlays(other.guid,anchor,true,overlayHeight);if(missileInfo)setMissileBlend(missileInfo->trans);else drawBlend=0;}
                if(!isObject&&!isMissile&&(other.monsterType&0x40))drawBlend=2; // a ghostly champion: half see-through
                // Another player's Whirlwind (turned copies) and Charge (earlier copies along its way), as drawCharacter draws them.
                auto copyAt=[&](unsigned facing,unsigned frameIndex,float dx,float dy) {
                    const auto& f=animation->frames[(facing*animation->directions/16)*perDirection+frameIndex%perDirection];
                    drawTile(f.image.width,f.image.height,(anchor[0]+dx+f.x+f.image.width*.5f-scene->width*.5f)*zoom+panX,(anchor[1]+dy+f.y+f.image.height*.5f-scene->height*.5f)*zoom+panY,&f.image,false);
                };
                const bool otherWhirlwind=otherSkill&&otherSkill->serverDoFunc==76&&other.mode==18;
                const bool otherCharging=otherSkill&&otherSkill->serverDoFunc==67&&fromTable&&other.sequenceMode==3;
                static std::map<uint32_t,std::pair<uint64_t,uint64_t>> chargeSteps; // per player: running steps, the tick last counted
                int trailCopies=0,stepX=0,stepY=0;
                if(otherPlayer) {
                    auto& steps=chargeSteps[other.guid];
                    steps.first=otherCharging?steps.first+(worldView.tick!=steps.second?1:0):0;steps.second=worldView.tick;
                    if(otherCharging&&steps.first) {
                        const auto now=d2::projectPoint(*scene,other.tickX,other.tickY),then=d2::projectPoint(*scene,other.lastX,other.lastY);
                        const int vx=int(std::lround(now[0]-then[0])),vy=int(std::lround(now[1]-then[1]));
                        const int ax=std::abs(vx),ay=std::abs(vy),length=std::max(1,(ax>ay?ay+2*ax:ax+2*ay)/2);
                        trailCopies=int(std::min<uint64_t>(steps.first,2));stepX=vx*15/length;stepY=vy*15/length;
                    }
                }
                auto drawTrail=[&](bool before) {
                    if(!trailCopies||before!=(stepY>=0))return;
                    int frameNow=int(frameInDirection);
                    for(int i=1;i<=trailCopies;++i){if(--frameNow<0)frameNow+=8;const int back=before?trailCopies+1-i:i;copyAt(other.facing,unsigned(frameNow),-float(back*stepX),-float(back*stepY));}
                };
                drawTrail(true);
                if(otherWhirlwind)copyAt(((other.facing*4+21)&63)/4,frameInDirection,0,0);
                drawTile(frame.image.width,frame.image.height,cx,cy,&frame.image,false);
                if(otherWhirlwind)copyAt(((other.facing*4+42)&63)/4,frameInDirection,0,0);
                drawTrail(false);
                if((isObject&&other.guid==hoveredObject)||(!isObject&&!isMissile&&other.guid==hoveredUnit&&(other.kind==1||other.kind==2))) {
                    // Under the cursor: lit up, as the game shows what can be used (the picture added onto itself, dimmed).
                    const auto saved=currentLight;for(auto& v:currentLight)v*=.4f;
                    drawBlend=1;drawTile(frame.image.width,frame.image.height,cx,cy,&frame.image,false);
                    currentLight=saved;
                }
                drawBlend=0;
                drawGlow(frame,cx,cy);
                if(isMissile&&missileInfo)drawLoopingOverlay(missileInfo->progressOverlay,other.ticks,anchor,false,2);
                if(!isObject&&!isMissile)drawStateOverlays(other.states,anchor,false,overlayHeight);
                // Overlays the game plays once over this unit (packet 0x11, skills.txt overlays), frame by frame from when they began.
                if(!isObject&&!isMissile)drawOneShotOverlays(other.guid,anchor,false,overlayHeight);
            };
            static const std::vector<uint32_t> noFrames;
            auto drawPlacement=[&](uint32_t index) {
                const auto& p=scene->placements[index];
                // An animated tile (water, lava) shows its next frame every 3 game ticks.
                const auto& frames=p.tile<scene->tileFrames.size()?scene->tileFrames[p.tile]:noFrames;
                const auto& tile=scene->tiles[frames.empty()?p.tile:frames[size_t(worldView.tick/3)%frames.size()]];const auto& img=tile.image;
                if(lightingOn) {
                    if(p.shadow)setLight({1,1,1},{1,1,1},{1,1,1},{1,1,1});
                    else if(d2::tileDrawsWithGround(tile)) {
                        // A floor-like tile lies on the ground: the light at its picture's corners, blended across it.
                        auto at=[&](float px,float py){const auto g=groundOf(px,py);return lightAt(g[0],g[1]);};
                        setLight(at(float(p.x),float(p.y)),at(float(p.x+img.width),float(p.y)),at(float(p.x),float(p.y+img.height)),at(float(p.x+img.width),float(p.y+img.height)));
                    } else {
                        // Walls and roofs stand on their footprint: shaded from its left end on screen (x0,y1) to its right end (x1,y0).
                        const auto f=d2::tileFootprint(tile,p.cellX,p.cellY);
                        const auto left=lightAt(f.x0,f.y1),right=lightAt(f.x1,f.y0);
                        setLight(left,right,left,right);
                    }
                }
                const float opacity=index<wallOpacity.size()?wallOpacity[index]:1.f;
                if(opacity<.999f){drawBlend=2;blendFactor=opacity;}
                drawTile(img.width,img.height,(p.x+img.width*.5f-scene->width*.5f)*zoom+panX,
                    (p.y+img.height*.5f-scene->height*.5f)*zoom+panY,&img,p.shadow);
                if(opacity<.999f){drawBlend=0;blendFactor=.5f;}
            };
            // Tiles standing in front of a unit are held back and drawn after it; everything else keeps the map's own
            // painter order. Units are drawn far to near, and a tile waits for the nearest unit it stands in front of.
            {
                // The object under the cursor (from the last frame's boxes, the one in front): drawn lit up, with its name over it.
                const float mx=(mouseX-width*.5f-panX)/zoom+scene->width*.5f,my=(mouseY-height*.5f-panY)/zoom+scene->height*.5f;
                float front=-1e30f;hoveredObject=0;hoveredUnit=0;
                for(const auto& box:objectPicks)if(!(box.guid&d2::tileIdBit)&&mx>=box.x&&mx<=box.x+box.w&&my>=box.y&&my<=box.y+box.h&&box.depth>front){front=box.depth;hoveredObject=box.guid;}
                front=-1e30f;
                for(const auto& box:unitPicks)if(mx>=box.x&&mx<=box.x+box.w&&my>=box.y&&my<=box.y+box.h&&box.depth>front){front=box.depth;hoveredUnit=box.guid;}
                touch::highlight(hoveredUnit,hoveredObject); // what the phone controls point at is lit up and named
            }
            objectPicks.clear();
            unitPicks.clear();
            // Level entrances: their clickable rectangle (lvlwarp.txt) is relative to the tile's screen position.
            if(warpTable&&walkGrid)for(const auto& tile:worldView.tiles) {
                const auto* warp=warpTable->find(tile.classId);if(!warp)continue;
                const auto q=d2::projectPoint(*scene,float(tile.x)+.5f,float(tile.y)+.5f);
                objectPicks.push_back({std::round(q[0])+warp->selectX,std::round(q[1])+warp->selectY,float(warp->selectWidth),float(warp->selectHeight),tile.id,q[1]+9999.f});
            }
            struct Standing {float x,y;int index;}; // index: -1 the player, otherwise an entry of `others`
            std::vector<Standing> standing;
            const bool withCharacter=idleAnimation&&walkGrid;
            if(withCharacter)standing.push_back({view.x,view.y,-1});
            for(size_t i=0;i<others.size();++i)if(others[i].kind!=0||others[i].lookClass!=255)standing.push_back({others[i].x,others[i].y,int(i)});
            std::stable_sort(standing.begin(),standing.end(),[](const Standing& a,const Standing& b){return a.x+a.y<b.x+b.y;});
            std::vector<std::vector<uint32_t>> waiting(standing.size()),roofsBefore(standing.size());
            std::vector<uint32_t> roofsLast;std::set<size_t> frontOfRoof;
            // The ground first (floors and the map's shadows), then the units' shadows on it, then walls and units in order.
            auto isGround=[&](const d2::TilePlacement& p){return p.shadow||d2::tileDrawsWithGround(scene->tiles[p.tile]);};
            for(auto index:visible)if(isGround(scene->placements[index]))drawPlacement(index);
            // Rain splashes and mud bubbles (D2Client 0x6FAA31BB, while drawing the floors): on a floor tile whose DT1 byte 6
            // has the water bit (2), a rain splash when rand(1000) < the rain's strength * 1000 (0x6FAA3367: its drops / 256),
            // a bubble when the level is muddy (levels.txt Mud) and rand(1000) < 100; at a random point of the tile's diamond
            // (0x6FAA7590), the rain's Rain3 or Rain4, the bubble's bubble3 30 pixels higher (0x6FAA7760), a frame every 2
            // ticks. The original checks each drawn frame (rain at most every 3 ms, bubbles every 25 ms); here once a 25 fps
            // frame for rain, so a faster screen doesn't rain harder.
            if(overlayArt&&walkGrid) {
                const auto now=std::chrono::steady_clock::now();
                const auto* here=levelTable?levelTable->byId(int32_t(worldView.level)):nullptr;
                const bool raining=!gameWeather.snowing()&&gameWeather.strength()>0;
                const bool rainTurn=raining&&now-lastRainSpawn>=std::chrono::milliseconds(40);
                const bool bubbleTurn=here&&here->mud&&now-lastBubbleSpawn>=std::chrono::milliseconds(25);
                if(rainTurn)lastRainSpawn=now;
                if(bubbleTurn)lastBubbleSpawn=now;
                const int rate=int(gameWeather.strength()*1000.f);
                if(rainTurn||bubbleTurn)for(auto index:visible) {
                    const auto& p=scene->placements[index];
                    if(p.shadow||scene->tiles[p.tile].orientation!=0||!(scene->tiles[p.tile].material&2))continue;
                    const int roll=int(splashRandom()%1000);
                    auto place=[&](int file,float rise) {
                        const int row=int(splashRandom()%80);const int half=80-2*std::abs(row-40);
                        const float x=float(p.x)+80.f+float(half>0?int(splashRandom()%uint32_t(2*half))-half:0),y=float(p.y)+float(row)-rise;
                        splashes.push_back({x,y,p.cellX*5.f+2.5f,p.cellY*5.f+2.5f,file,worldView.tick});
                    };
                    if(rainTurn&&roll<rate)place(int(2+(splashRandom()&1)),0);
                    if(bubbleTurn&&roll<100)place(4,30);
                    if(splashes.size()>600)break;
                }
                if((rainTurn||bubbleTurn)&&std::getenv("D2MODERN_WEATHER_LOG")){size_t water=0;for(auto index:visible){const auto& p=scene->placements[index];if(!p.shadow&&scene->tiles[p.tile].orientation==0&&(scene->tiles[p.tile].material&2))++water;}std::cout<<"Splashes: "<<splashes.size()<<" on "<<water<<" water tiles in view (rate "<<rate<<"/1000)"<<std::endl;}
                static const char* files[]={"","","data\\global\\UncompOverlays\\Rain3.dc6","data\\global\\UncompOverlays\\Rain4.dc6","data\\global\\UncompOverlays\\bubble3.dc6"};
                drawBlend=0;blendFactor=1.f;
                std::erase_if(splashes,[&](const Splash& s){
                    const auto* art=overlayArt->dc6(files[s.file]);
                    const uint64_t frame=(worldView.tick-s.start)/2;
                    if(!art||frame>=art->frames.size()||worldView.tick<s.start)return true;
                    const auto& f=art->frames[size_t(frame)];
                    lightStanding(s.subX,s.subY);
                    const float cx=(s.x+float(f.x)+f.image.width*.5f-scene->width*.5f)*zoom+panX,cy=(s.y+float(f.y)+f.image.height*.5f-scene->height*.5f)*zoom+panY;
                    drawTile(f.image.width,f.image.height,cx,cy,&f.image,false);
                    return false;
                });
            }
            for(const auto& s:standing){if(s.index<0)drawCharacter(true);else drawOther(others[size_t(s.index)],true);}
            // Fade a connected-looking patch rather than only the exact wall pixels over the player. Roofs participate too,
            // which opens the view naturally when walking into the larger town buildings.
            std::vector<float> fadeTarget(scene->placements.size(),1.f);std::vector<uint32_t> seeds;
            auto buildingPiece=[](const d2::Tile& tile){return (tile.orientation>=1&&tile.orientation<=12)||tile.orientation==15;};
            if(playerFrame)for(auto index:visible) {
                const auto& p=scene->placements[index];const auto& tile=scene->tiles[p.tile];const auto& img=tile.image;
                if(p.shadow||!buildingPiece(tile)||!d2::drawnAfterUnit(tile,p.cellX,p.cellY,view.x,view.y))continue;
                const int x0=std::max(p.x,int(playerBox[0])),y0=std::max(p.y,int(playerBox[1]));
                const int x1=std::min(p.x+int(img.width),int(playerBox[0]+playerBox[2])),y1=std::min(p.y+int(img.height),int(playerBox[1]+playerBox[3]));
                int covered=0;
                for(int y=y0;y<y1&&covered<40;++y)for(int x=x0;x<x1&&covered<40;++x)
                    if(img.rgba[(size_t(y-p.y)*img.width+size_t(x-p.x))*4+3]&&playerFrame->image.rgba[(size_t(y-int(playerBox[1]))*playerFrame->image.width+size_t(x-int(playerBox[0])))*4+3])++covered;
                if(covered>=24){fadeTarget[index]=wallFade;seeds.push_back(index);}
            }
            for(auto index:visible) {
                const auto& p=scene->placements[index];const auto& tile=scene->tiles[p.tile];if(p.shadow||!buildingPiece(tile))continue;
                for(auto seed:seeds) {
                    const auto& q=scene->placements[seed];const auto& seedTile=scene->tiles[q.tile];const int radius=seedTile.orientation==15?6:4;
                    const int distance=std::max(std::abs(int(p.cellX)-int(q.cellX)),std::abs(int(p.cellY)-int(q.cellY)));
                    if(distance>radius)continue;
                    const float edge=std::clamp(float(distance-1)/float(std::max(1,radius-1)),0.f,1.f);
                    const float smooth=edge*edge*(3.f-2.f*edge);
                    fadeTarget[index]=std::min(fadeTarget[index],wallFade+(1.f-wallFade)*smooth);
                }
            }
            if(wallOpacity.size()!=scene->placements.size())wallOpacity.assign(scene->placements.size(),1.f);
            const auto fadeNow=std::chrono::steady_clock::now();
            const float fadeSeconds=wallFadeTime.time_since_epoch().count()?std::clamp(std::chrono::duration<float>(fadeNow-wallFadeTime).count(),0.f,.1f):1.f/60.f;
            wallFadeTime=fadeNow;
            for(size_t i=0;i<wallOpacity.size();++i) {
                const float target=fadeTarget[i];
                const float rate=target<wallOpacity[i]?10.f:6.f;
                const float amount=1.f-std::exp(-rate*fadeSeconds);
                wallOpacity[i]+= (target-wallOpacity[i])*amount;
                if(std::abs(wallOpacity[i]-target)<.002f)wallOpacity[i]=target;
            }
            for(auto index:visible) {
                const auto& p=scene->placements[index];
                if(isGround(p))continue;
                if(!p.shadow&&scene->tiles[p.tile].orientation==15) {
                    // A roof covers everything under it (walls and units inside or behind the building); units standing in front of it
                    // are drawn again afterwards (below).
                    roofsLast.push_back(index);
                    continue;
                }
                int after=-1;
                if(!p.shadow)for(size_t u=0;u<standing.size();++u)if(d2::drawnAfterUnit(scene->tiles[p.tile],p.cellX,p.cellY,standing[u].x,standing[u].y))after=int(u);
                if(after<0)drawPlacement(index);else waiting[size_t(after)].push_back(index);
            }
            for(size_t u=0;u<standing.size();++u) {
                if(standing[u].index<0)drawCharacter();else drawOther(others[size_t(standing[u].index)]);
                for(auto index:waiting[u])drawPlacement(index);
            }
            // A unit goes over the roofs only when every roof piece its picture overlaps lies behind it: one in front of a
            // building. A cow in a barn has roof pieces in front of it as well, and stays under the roof.
            for(size_t u=0;u<standing.size();++u) {
                const auto at=d2::projectPoint(*scene,standing[u].x,standing[u].y);
                const float ux0=at[0]-40,ux1=at[0]+40,uy0=at[1]-120,uy1=at[1]+10; // about a unit's picture around its feet
                bool overlaps=false,covered=false;
                for(auto index:roofsLast) {
                    const auto& p=scene->placements[index];const auto& img=scene->tiles[p.tile].image;
                    if(ux1<p.x||ux0>p.x+int(img.width)||uy1<p.y||uy0>p.y+int(img.height))continue;
                    overlaps=true;
                    if(d2::drawnAfterUnit(scene->tiles[p.tile],p.cellX,p.cellY,standing[u].x,standing[u].y)){covered=true;break;}
                }
                if(overlaps&&!covered)frontOfRoof.insert(u);
            }
            for(auto index:roofsLast)drawPlacement(index);
            for(auto u:frontOfRoof){if(standing[u].index<0)drawCharacter();else drawOther(others[size_t(standing[u].index)]);}
        } else drawTile(spriteWidth,spriteHeight,panX,panY,&viewedImage,false);
        setLight({1,1,1},{1,1,1},{1,1,1},{1,1,1}); // overlays and the interface are not lit
        if(scene&&walkGrid) {
            auto drawCell=[&](d2::GridPoint p,const d2::Image* glyph) {
                auto pos=d2::projectSubtile(*scene,p);
                const float x=(pos[0]-scene->width*.5f)*zoom+panX;
                const float y=(pos[1]-scene->height*.5f)*zoom+panY;
                if(x+16*zoom<-float(width)*.5f||x-16*zoom>width*.5f||y+8*zoom<-float(height)*.5f||y-8*zoom>height*.5f)return;
                drawTile(32,16,x,y,glyph,false);
            };
            if(showCollision)for(int y=0;y<int(walkGrid->height);++y)for(int x=0;x<int(walkGrid->width);++x) {
                // Missing floors are blocked, but avoid painting the surrounding void.
                if(walkGrid->ground[size_t(y)*walkGrid->width+x])drawCell({x,y},walkGrid->walkable({x,y})?&freeGlyph:&blockedGlyph);
            }
            if(!idleAnimation||showCollision)drawCell({int(std::floor(view.x)),int(std::floor(view.y))},&markerGlyph);
            if(!unitArt)for(const auto& other:others)drawCell({int(std::floor(other.x)),int(std::floor(other.y))},other.kind==1?&townPersonGlyph:other.kind==2?&enemyGlyph:&corpseGlyph);
        }
        if(scene&&walkGrid) {
            // Rain and snow (Weather.h) over the world, under the interface: lines in the game screen's pixels, a pixel at a time
            // as D2Gfx draws lines, at their colour and opacity.
            weatherScreenWidth=float(width)/zoom;weatherScreenHeight=float(height)/zoom;
            // D2Client takes each colour's nearest entry of the act's palette (D2Win #10034), which tames the colours its byte
            // arithmetic wraps round to.
            static std::map<uint32_t,std::array<uint8_t,3>> nearest;static const d2::Palette* nearestOf=nullptr;
            if(nearestOf!=unitPalette){nearest.clear();nearestOf=unitPalette;}
            auto paletteColor=[&](std::array<uint8_t,3> rgb) {
                if(!unitPalette)return rgb;
                const uint32_t key=uint32_t(rgb[0])<<16|uint32_t(rgb[1])<<8|rgb[2];
                if(auto f=nearest.find(key);f!=nearest.end())return f->second;
                int best=1<<30;std::array<uint8_t,3> out=rgb;
                for(const auto& e:*unitPalette) {
                    const int dr=int(e[0])-rgb[0],dg=int(e[1])-rgb[1],db=int(e[2])-rgb[2],d=dr*dr+dg*dg+db*db;
                    if(d<best){best=d;out={e[0],e[1],e[2]};}
                }
                return nearest[key]=out;
            };
            auto pixel=[&](float gx,float gy,const d2::Weather::Line& l) {
                if(gx<0||gy<0||gx>=weatherScreenWidth||gy>=weatherScreenHeight)return;
                d2::DrawCommand c;c.color=paletteColor(l.color);
                c.quad={zoom/width,zoom/height,2*((gx+.5f)*zoom-width*.5f)/width,-2*((gy+.5f)*zoom-height*.5f)/height};
                if(l.alpha<1){c.blend=d2::DrawCommand::Blend::Mix;c.amount=l.alpha;}
                commands.push_back(c);
            };
            for(const auto& l:gameWeather.lines()) {
                const int steps=int(std::max(std::abs(l.x1-l.x0),std::abs(l.y1-l.y0)));
                for(int s=0;s<=steps;++s) {
                    const float t=steps?float(s)/float(steps):0.f;
                    pixel(std::floor(l.x0+(l.x1-l.x0)*t),std::floor(l.y0+(l.y1-l.y0)*t),l);
                }
            }
        }
        if(worldView.vitals.maxLife>0||gameMenuPage!=GameMenuPage::Closed)drawHud();
        gpu->draw(commands); // the world, then the interface, drawn in order
        return true;
    }
