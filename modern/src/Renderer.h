// The renderer: what the game looks like, as draw lists (DrawList.h) — the world (RenderWorld.cpp), the interface
// (RenderHud.cpp), the menus and the cursor — handed to a GPU backend (GpuBackend.h; Direct3D 11 on Windows). Nothing in
// here calls a graphics API.
#pragma once
#include <random>
#include "ClientState.h"
#include "ItemStats.h"
#include "Hud.h"
#include "DrawList.h"
#include "GpuBackend.h"
#include "PlatformText.h"
#include "TouchControls.h"
#include <chrono>
#include <unordered_set>
class Renderer {
    std::unique_ptr<d2::GpuBackend> gpu;
    const d2::MapScene* scene=nullptr;
    // Small code-generated debug glyphs (collision cells, markers, units without art); these are not character artwork.
    d2::Image blockedGlyph,freeGlyph,markerGlyph,townPersonGlyph,enemyGlyph,corpseGlyph;
    d2::Image viewedImage; // the asset viewer's picture (no map)
    const d2::Animation* idleAnimation=nullptr;
    const d2::Animation* walkAnimation=nullptr;
    std::map<uint8_t,const d2::Animation*> movementAnimations;
    // What the world and the interface want drawn this frame, in order, and the light the next world picture gets.
    d2::DrawList commands;
    std::array<float,12> currentLight{1,1,1,1,1,1,1,1,1,1,1,1};
    uint32_t width=0,height=0,spriteWidth=0,spriteHeight=0;
    std::vector<float> wallOpacity;
    std::chrono::steady_clock::time_point wallFadeTime;
public:
    explicit Renderer(std::unique_ptr<d2::GpuBackend> backend):gpu(std::move(backend)) {}
    bool software() const {return gpu->software();}
    std::string backendName() const {return gpu->name();}
    size_t visibleCount=0;
    CharacterView view;
    // Other native units, drawn as debug glyphs: kind 1 town person (blue), 2 enemy (red), 3 corpse (grey).
    struct OtherUnit {float x,y;uint8_t kind;unsigned facing;uint32_t classId,mode,guid;uint64_t ticks;uint16_t speed;std::array<uint32_t,6> states{};std::array<uint8_t,16> components{};uint8_t colorTransform=0,monsterType=0;uint8_t lookClass=255,lookMode=0;std::array<uint32_t,17> look{};
        // Another player (LAN): its skill sequence (table mode and frame, Leap's frame, the skill) and where it stood a tick ago (Charge's trail).
        uint8_t sequenceMode=255,sequenceFrame=0,skillMoveFrame=255;uint16_t usedSkill=0;float tickX=0,tickY=0,lastX=0,lastY=0;}; // components, colour, type, player look: monsters (EntitySnapshot)
    d2::UnitArt* unitArt=nullptr; // monster and NPC animations, when the game data is available
    const d2::StringTable* strings=nullptr;
    const d2::ItemTable* itemTable=nullptr;
    const d2::ItemStatTable* itemStats=nullptr; // itemstatcost.txt: the items' magic property lines
    d2::UIAssets* uiAssets=nullptr; // the game's interface pictures; without them the interface is drawn as plain boxes
    std::vector<int64_t> levelExperience; // experience.txt: row i is the experience that reaches level i+1
    float insideAmbient=0.15f,playerLightRadius=11; // light where nothing lights an inside level, and the player's light radius in subtiles
    d2::TextCache* text42=nullptr; // large gothic menu lettering
    d2::TextCache* text30=nullptr; // font30: the death screen's lines
    d2::TextCache* text16=nullptr; // the game's font16 for panel text
    d2::TextCache* text8=nullptr;  // smaller game fonts for labels that do not fit their box in font16
    d2::TextCache* text6=nullptr;
    std::vector<OtherUnit> others;
    // In-game text in the game's font16 (the system font only before the game's fonts are loaded).
    std::map<std::string,d2::Image> fallbackText; // system font pictures, kept (their addresses key textures)
    const d2::Image& textImage(const std::string& s,uint8_t r,uint8_t g,uint8_t b) {
        if(text16)return text16->get(s,{r,g,b},true);
        const std::string key=std::to_string((int(r)<<16)|(int(g)<<8)|b)+":"+s;
        if(auto found=fallbackText.find(key);found!=fallbackText.end())return found->second;
        return fallbackText.emplace(key,d2::systemTextImage(s,{r,g,b})).first->second;
    }
    d2::ItemArt* itemArt=nullptr;
    const d2::ObjectTable* objectTable=nullptr;
    d2::ObjectArt* objectArt=nullptr;
    const d2::LevelTable* levelTable=nullptr;
    const d2::AutomapTable* automapTable=nullptr;
    // The automap: per map (automapKey), the world cells the player has seen; for the loaded scene the same as a bitmap, each
    // cell's level, and the rooms revealed (prepareSeen).
    std::map<uint32_t,std::unordered_set<int64_t>> revealed;
    std::vector<uint8_t> sceneSeen,seenRooms;std::vector<int16_t> sceneCellLevel;
    const d2::MapScene* seenScene=nullptr;d2::GridPoint seenOrigin{-1,-1};const d2::NativeTown::Room* currentRoom=nullptr;
    const d2::MissileTable* missileTable=nullptr;
    const d2::OverlayTable* overlayTable=nullptr;
    const d2::StateTable* stateTable=nullptr;
    const d2::StateColors* stateColors=nullptr; // Pal.PL2 remaps for states.txt colorshift
    const d2::Palette* unitPalette=nullptr; // the act palette the player's sprites are decoded with
    const d2::Archives* archives=nullptr; // used to decode a state-tinted copy of the player's current mode
    const d2::CharacterAppearance* playerLook=nullptr;
    d2::OverlayArt* overlayArt=nullptr;
    // Rain splashes and mud bubbles on water tiles (D2Client 0x6FAA31BB): scene pixel position, the subtile it lights by,
    // which (Rain3/Rain4 or bubble3) and the game tick it began.
    struct Splash {float x,y,subX,subY;int file;uint64_t start;};
    std::vector<Splash> splashes;std::chrono::steady_clock::time_point lastRainSpawn{},lastBubbleSpawn{};
    std::mt19937 splashRandom{std::random_device{}()};
    // The overlay playing on the player this frame (a cast glow): animation, frame, draw effect and the light it gives.
    struct PlayerOverlay {const d2::Animation* animation=nullptr;unsigned frame=0;int x=0,y=0,trans=3;float radius=0;std::array<float,3> color{1,1,1};bool behind=false;} playerOverlay;
    const d2::WarpTable* warpTable=nullptr;
    d2::MissileArt* missileArt=nullptr;
    const d2::SkillTable* skillTable=nullptr;
    d2::SkillArt* skillArt=nullptr;
    // A picture darkened to 35% (skills not learned yet).
    std::map<const d2::Image*,d2::Image> dimImages;
    const d2::Image& dimmedImage(const d2::Image& image) {
        if(auto found=dimImages.find(&image);found!=dimImages.end())return found->second;
        d2::Image copy=image;
        for(size_t i=0;i+3<copy.rgba.size();i+=4)for(int c=0;c<3;++c)copy.rgba[i+c]=uint8_t(copy.rgba[i+c]*35/100);
        return dimImages.emplace(&image,std::move(copy)).first->second;
    }
    // A frame's shadow on the ground: its shadow-casting pixels pressed to half height and leaning up to the left at 45 degrees on
    // screen. (The lean is judged by eye: the map's pre-drawn shadow tiles lean less, but then a unit's shadow hides behind
    // the unit itself.) x,y: its top-left corner from the anchor.
    struct ShadowImage {d2::Image image;int x=0,y=0;bool any=false;};
    std::map<const d2::AnimationFrame*,ShadowImage> shadowImages;
    const ShadowImage* shadowOf(const d2::AnimationFrame& frame) {
        if(!frame.shadow||frame.image.rgba.empty())return nullptr;
        if(auto found=shadowImages.find(&frame);found!=shadowImages.end())return found->second.any?&found->second:nullptr;
        const auto& mask=frame.shadow==2?frame.shadowMask:frame.image;
        auto floorDiv=[](int a,int b){return a>=0?a/b:-((-a+b-1)/b);};
        const int first=frame.y,last=frame.y+int(frame.image.height)-1;
        constexpr int lean=2; // a picture row y above the feet moves y/lean to the left, and y/2 up
        const int top=floorDiv(first,2),left=frame.x+std::min(floorDiv(first,lean),floorDiv(last,lean));
        const uint32_t w=frame.image.width+uint32_t(std::abs(floorDiv(last,lean)-floorDiv(first,lean))),h=uint32_t(floorDiv(last,2)-top+1);
        d2::Image out{w,h,d2::Bytes(size_t(w)*h*4,0)};bool any=false;
        for(uint32_t y=0;y<frame.image.height;++y) {
            const int ay=frame.y+int(y);const size_t row=size_t(floorDiv(ay,2)-top)*w;const int shift=frame.x+floorDiv(ay,lean)-left;
            for(uint32_t x=0;x<frame.image.width;++x)if(mask.rgba[(size_t(y)*frame.image.width+x)*4+3]){out.rgba[(row+size_t(shift+int(x)))*4+3]=255;any=true;}
        }
        ShadowImage s;s.x=left;s.y=top;s.any=any;s.image=std::move(out);
        auto& stored=shadowImages.emplace(&frame,std::move(s)).first->second;
        return stored.any?&stored:nullptr;
    }
    // A new map: its tiles are drawn from their pictures (textures are made when first drawn); the old map's pictures are
    // gone, so every cached texture is dropped.
    void upload(const d2::MapScene& map) {
        forgetImages();
        auto glyph=[&](std::array<uint8_t,4> color,bool outline) {
            d2::Image img{32,16,d2::Bytes(32*16*4,0)};
            for(int y=0;y<16;++y)for(int x=0;x<32;++x) {
                float d=std::abs((x-15.5f)/16)+std::abs((y-7.5f)/8);
                if(d<=1&&(!outline||d>.7f))std::copy(color.begin(),color.end(),img.rgba.begin()+(y*32+x)*4);
            }
            return img;
        };
        blockedGlyph=glyph({255,65,65,255},true);
        freeGlyph=glyph({50,185,100,255},true);
        markerGlyph=glyph({255,220,40,255},false);
        townPersonGlyph=glyph({80,170,255,255},false);
        enemyGlyph=glyph({235,40,40,255},false);
        corpseGlyph=glyph({130,130,130,255},true);
        scene=&map;spriteWidth=map.width;spriteHeight=map.height;
        wallOpacity.assign(map.placements.size(),1.f);wallFadeTime={};
        size_t bytes=0;for(const auto& t:map.tiles)bytes+=t.image.rgba.size();
        std::cout<<"GPU scene: "<<map.tiles.size()<<" shared tile textures | "<<bytes<<" texture bytes | "<<map.placements.size()<<" placements\n";
    }
    // The asset viewer's picture (no map), kept here; its texture is remade.
    void show(d2::Image image) {
        gpu->forgetTexture(&viewedImage);
        viewedImage=std::move(image);scene=nullptr;spriteWidth=viewedImage.width;spriteHeight=viewedImage.height;
    }
    void uploadCharacters(const d2::Animation& idle,const d2::Animation& walking){idleAnimation=&idle;walkAnimation=&walking;}
    // Before the player's animations are replaced (new gear): drop every mode, since the new gear may lack some.
    void clearMovement(){movementAnimations.clear();playerTints.clear();}
    void uploadMovement(uint8_t mode,const d2::Animation& animation){movementAnimations[mode]=&animation;}
    // A front-end screen: 800x600 art scaled to fit the window, centred on black. Leaves the placement in menuScale/menuLeft/menuTop for mouse input.
    bool drawScreen(const std::vector<d2::ScreenSprite>& sprites) {
        if(!begin({0,0,0,1}))return false;
        menuScale=std::min(width/800.f,height/600.f);menuLeft=(width-800*menuScale)*.5f;menuTop=(height-600*menuScale)*.5f;
        for(const auto& s:sprites) {
            if(s.dynamic)gpu->forgetTexture(s.image);
            const float w=s.image->width*menuScale,h=s.image->height*menuScale,x=menuLeft+s.x*menuScale,y=menuTop+s.y*menuScale;
            d2::DrawCommand c;c.image=s.image;c.quad={w/width,h/height,2*(x+w*.5f-width*.5f)/width,-2*(y+h*.5f-height*.5f)/height};
            c.blend=s.additive?d2::DrawCommand::Blend::Add:d2::DrawCommand::Blend::Normal;
            commands.push_back(c);
        }
        gpu->draw(commands);
        return true;
    }
    // Pictures are drawn from textures cached by the picture's address: drop them before those pictures are freed, so a new
    // picture at the same address is not drawn with an old texture.
    void forgetImages(){dimImages.clear();shadowImages.clear();gpu->forgetTextures();}
    // The player's mode decoded through a states.txt colorshift (Pal.PL2), or the untinted animation when shift is 0.
    // Kept until the gear changes (clearMovement). A level change only drops textures, so these pictures stay.
    const d2::Animation* tintedPlayer(uint8_t mode,const d2::Animation& untinted,uint8_t shift);
    std::map<std::pair<uint8_t,uint8_t>,std::unique_ptr<d2::Animation>> playerTints;
    // Lighting. Levels that are inside (caves, crypts, dungeons: levels.txt IsInside) are dark but for the light around the
    // player and around lit objects and missiles. Outside, the act's time of day lights everything (WorldSnapshot envColor
    // and envIntensity, 128 being full light), with the same lights added at dusk and night. Positions and radii are in subtiles.
    struct LightSource {float x,y,radius;std::array<float,3> color;};
    std::vector<LightSource> lights;std::array<float,3> ambient{1,1,1};bool lightingOn=false;
    std::array<float,3> lightAt(float x,float y) const {
        if(!lightingOn)return {1,1,1};
        std::array<float,3> l=ambient;
        for(const auto& s:lights) {
            const float d=std::hypot(x-s.x,y-s.y);if(d>=s.radius)continue;
            float f=1-d/s.radius;f*=2-f; // full near the source, fading out to its radius
            for(int k=0;k<3;++k)l[k]+=s.color[k]*f;
        }
        for(auto& v:l)v=std::min(v,1.f);
        return l;
    }
    // The light of the next world pictures, at their corners: top left, top right, bottom left, bottom right.
    void setLight(std::array<float,3> a,std::array<float,3> b,std::array<float,3> c,std::array<float,3> d) {
        currentLight={a[0],a[1],a[2],b[0],b[1],b[2],c[0],c[1],c[2],d[0],d[1],d[2]};
    }
    // A new frame at the window's size, with nothing to draw yet.
    bool begin(const std::array<float,4>& clear) {
        if(!gpu->begin(clear))return false;
        width=gpu->width();height=gpu->height();
        commands.clear();currentLight={1,1,1,1,1,1,1,1,1,1,1,1};
        return true;
    }
    // The world (map tiles, units, shadows, light: RenderWorld.cpp) and the interface over it.
    bool draw();
    // The automap map a level draws on: its act and its levels.txt Layer (an act's outdoor areas share one).
    uint32_t automapKey(int32_t level) const {
        const uint32_t act=level<40?0:level<75?1:level<103?2:level<109?3:4;
        const auto* info=levelTable?levelTable->byId(level):nullptr;
        return (act<<16)|uint32_t(info?info->layer:level);
    }
    // For the loaded scene: which cells are seen, each cell's level, and which rooms were revealed. Rebuilt from `revealed`
    // (world cells, kept for the whole game) whenever another scene is loaded, so a map is never forgotten.
    void prepareSeen() {
        const size_t cells=size_t(scene->cellsX)*scene->cellsY;
        if(seenScene==scene&&seenOrigin==sceneOrigin&&sceneSeen.size()==cells&&seenRooms.size()==sceneRooms.size())return;
        seenScene=scene;seenOrigin=sceneOrigin;currentRoom=nullptr;
        sceneSeen.assign(cells,0);sceneCellLevel.assign(cells,int16_t(worldView.level));seenRooms.assign(sceneRooms.size(),0);
        for(const auto& room:sceneRooms)
            for(int cy=room.y/5;cy<(room.y+room.height)/5;++cy)for(int cx=room.x/5;cx<(room.x+room.width)/5;++cx)
                if(cx>=0&&cy>=0&&cx<int(scene->cellsX)&&cy<int(scene->cellsY))sceneCellLevel[size_t(cy)*scene->cellsX+cx]=int16_t(room.level);
        const int ox=sceneOrigin.x/5,oy=sceneOrigin.y/5;
        for(int cy=0;cy<int(scene->cellsY);++cy)for(int cx=0;cx<int(scene->cellsX);++cx) {
            const size_t at=size_t(cy)*scene->cellsX+cx;
            const auto found=revealed.find(automapKey(sceneCellLevel[at]));
            if(found!=revealed.end()&&found->second.count(cellKey(cx+ox,cy+oy)))sceneSeen[at]=1;
        }
    }
    static int64_t cellKey(int x,int y){return (int64_t(y)<<32)|uint32_t(x);}
    void revealCell(int cx,int cy,int32_t level) {
        if(cx<0||cy<0||cx>=int(scene->cellsX)||cy>=int(scene->cellsY))return;
        sceneSeen[size_t(cy)*scene->cellsX+cx]=1;
        revealed[automapKey(level)].insert(cellKey(cx+sceneOrigin.x/5,cy+sceneOrigin.y/5));
    }
    // The player reveals the map as they go: as the game does, the room they stand in and the rooms touching it (without
    // the game's rooms, the prototype world, the cells within 7 of the player).
    void revealAround(float x,float y) {
        if(!scene)return;
        prepareSeen();
        if(sceneRooms.empty()) {
            constexpr int radius=7; // map cells (5 subtiles each)
            const int px=int(std::floor(x/5)),py=int(std::floor(y/5));
            for(int cy=py-radius;cy<=py+radius;++cy)for(int cx=px-radius;cx<=px+radius;++cx)
                if((cx-px)*(cx-px)+(cy-py)*(cy-py)<=radius*radius)revealCell(cx,cy,int32_t(worldView.level));
            return;
        }
        const d2::NativeTown::Room* here=nullptr;
        for(const auto& room:sceneRooms)if(x>=room.x&&x<room.x+room.width&&y>=room.y&&y<room.y+room.height)here=&room;
        if(!here||(currentRoom==here&&seenRooms[size_t(here-sceneRooms.data())]))return;
        currentRoom=here;
        for(size_t i=0;i<sceneRooms.size();++i) {
            const auto& room=sceneRooms[i];
            if(seenRooms[i]||room.x>here->x+here->width||room.x+room.width<here->x||room.y>here->y+here->height||room.y+room.height<here->y)continue;
            seenRooms[i]=1;
            for(int cy=room.y/5;cy<(room.y+room.height)/5;++cy)for(int cx=room.x/5;cx<(room.x+room.width)/5;++cx)revealCell(cx,cy,room.level);
        }
    }
    // An item name's colour, as the game shows it: grey for low quality, blue magic, green set, yellow rare, gold unique,
    // orange crafted, white otherwise. Magic through crafted stay red until IFLAG_IDENTIFIED (flags bit 0x10).
    static std::array<uint8_t,3> itemColor(const d2::ItemSnapshot& item) {
        if((item.flags&0x10)==0&&item.quality>=4&&item.quality<=8)return {255,77,77};
        switch(item.quality){case 1:return {105,105,105};case 4:return {105,105,255};case 5:return {0,255,0};case 6:return {255,255,100};case 7:return {199,179,119};case 8:return {255,168,0};default:return {255,255,255};}
    }
    // Interface: the control panel, panels, menus, labels. RenderHud.cpp builds it as a list of sprites; they are added to
    // the frame's commands.
    void buildHud(std::vector<d2::HudSprite>& hud);
    void drawHud() {
        std::vector<d2::HudSprite> hud;buildHud(hud);
        touch::Art art;
        art.skill=[this](int skill){return skillIconImage(skill);};
        art.item=[this](int32_t id)->const d2::Image* {
            if(!itemArt)return nullptr;
            for(const auto& item:worldView.items)if(int32_t(item.id)==id)return itemArt->get(item.code,item.quality,item.invGraphic);
            return nullptr;
        };
        art.hand=uiAssets?uiAssets->frame("cursor\\ohand",0):nullptr;
        art.panelButton=[this](int button,bool pressed)->const d2::Image* {
            return uiAssets?uiAssets->frame("panel\\minipanelbtn",uint32_t(button*2+(pressed?1:0))):nullptr;
        };
        art.text=[this](const std::string& line,std::array<uint8_t,3> color)->const d2::Image*{
            return &textImage(line,color[0],color[1],color[2]);
        };
        touch::draw(hud,int(width),int(height),art);
        appendSprites(hud);
    }
    // A skill's icon (the phone controls' buttons), null when the art is not loaded.
    const d2::Image* skillIconImage(int skill) {
        if(!skillTable||!skillArt)return nullptr;
        const auto* info=skillTable->find(uint32_t(skill));
        return info?skillArt->get(info->charClass,info->iconCel):nullptr;
    }
    // Interface sprites (window pixels) as draw commands, unlit.
    void appendSprites(const std::vector<d2::HudSprite>& sprites) {
        for(const auto& s:sprites) {
            d2::DrawCommand c;c.image=s.image;c.color=s.color;c.amount=s.amount;
            c.quad={s.w/width,s.h/height,2*(s.x+s.w*.5f-width*.5f)/width,-2*(s.y+s.h*.5f-height*.5f)/height};
            c.blend=s.blend==d2::HudSprite::Blend::Mix?d2::DrawCommand::Blend::Mix:s.blend==d2::HudSprite::Blend::Darken?d2::DrawCommand::Blend::Darken:s.blend==d2::HudSprite::Blend::Scale?d2::DrawCommand::Blend::Scale:d2::DrawCommand::Blend::Normal;
            commands.push_back(c);
        }
    }
    // The game's cursor: the open hand (ohand), which closes (ppress) when a button goes down and turns (protate) while it is
    // held, at 25 frames a second. An item held on the cursor is drawn instead of the hand. A spell cursor (packet 0x3F)
    // is that frame of cursor\buysell; identify is frame 0, the question mark. A missing frame keeps the hand.
    // The hand cursor's state (drawCursor): 1 normal, 2 opening, 3 closing, 4 turning open, 5 pressed; which file (0 ohand,
    // 1 orotate, 2 ppress, 3 protate) and its frame in 256ths; the mouse and button last seen, the last event and step.
    int handState=1,handFile=3,handFrame=0,handMouseX=-1,handMouseY=-1;bool handHeld=false;uint64_t handLastEvent=0,handStep=0;
    std::mt19937 handRandom{std::random_device{}()};
    void drawCursor() {
        if(!uiAssets||mouseX<0||mouseY<0||mouseX>=int(width)||mouseY>=int(height))return;
        if(!frontEnd&&cursorItem)return;
        const float scale=std::max(1.f,std::min(width/800.f,height/600.f));
        // In a store the cursor is cursor\buysell in the mode of the store's buttons (D2Client's button records at 0x6FAE91C3,
        // set through 0x6FB57580) once one is chosen: buying frame 3, selling 4, repairing 1 (the hammer); the hand before that.
        const bool inStore=npcDialog.open&&npcDialog.page==NpcDialog::Page::Shop; // gambling buys too
        const int storeFrame=inStore?storeCursor:-1; // -1 until a store button is chosen: the hand
        if(std::getenv("D2MODERN_CURSOR_LOG")) {
            static int before=-2;const int now=spellCursor>=0?spellCursor:storeFrame;
            if(now!=before){std::cout<<"Cursor: "<<(now<0?std::string("hand"):"buysell frame "+std::to_string(now))<<"\n";before=now;}
        }
        if(!frontEnd&&(spellCursor>=0||storeFrame>=0)) {
            const uint32_t index=uint32_t(spellCursor>=0?spellCursor:storeFrame);
            if(const auto* image=uiAssets->frame("cursor\\buysell",index)) {
                // Hung from the pointer like the hand (top left at the hot spot); buysell's frame offsets would put it above the pointer.
                d2::HudSprite s;s.image=image;s.w=image->width*scale;s.h=image->height*scale;s.x=float(mouseX);s.y=float(mouseY);
                commands.clear();appendSprites({s});gpu->draw(commands);return;
            }
        }
        // The hand (D2Client 0x6FB57700, its table at 0x6FB96B54: per cursor whether it loops, 8 frames and the step per drawn
        // frame in 256ths): normally the pointing hand turning now and then (protate: 1/8 frame on a quarter of the frames);
        // pressed, the press looping (ppress, 1/4 frame) while a button is held; after 5 seconds without a mouse event the hand
        // opens (ohand forward, 1/4 frame) and turns open (orotate, 1/8 frame, looping); moved again, it closes (ohand
        // backwards) and points again. The original steps once a drawn frame at its 25 a second; here 25 times a second.
        enum {Normal=1,Opening=2,Closing=3,Turning=4,Pressed=5};
        const uint64_t now=nowMilliseconds();
        const bool held=leftButtonDown||rightButtonDown;
        if(mouseX!=handMouseX||mouseY!=handMouseY||held!=handHeld) {
            handLastEvent=now;
            if(mouseX!=handMouseX||mouseY!=handMouseY)if(handState==Opening||handState==Turning){handState=Closing;handFile=0;handFrame=7*256;}
            if(held&&!handHeld&&handState!=Pressed){handState=Pressed;handFile=2;handFrame=0;}
            if(!held&&handHeld&&handState==Pressed){handState=Normal;handFile=3;handFrame=0;}
            handMouseX=mouseX;handMouseY=mouseY;handHeld=held;
        }
        if(!handStep)handStep=now;
        for(int steps=0;now>=handStep+40&&steps<10;++steps) {
            handStep+=40;
            static const int stepOf[]={0x40,0x20,0x40,0x40}; // ohand, orotate, ppress, protate
            static const bool loops[]={false,true,true,true};
            if(handState==Closing){handFrame-=0x40;if(handFrame<0){handState=Normal;handFile=3;handFrame=0;}continue;}
            if(handState==Normal){if(handRandom()%64<16)handFrame+=0x20;}
            else handFrame+=stepOf[handFile];
            if(handFrame>=8*256) {
                if(loops[handFile])handFrame-=8*256;
                else if(handState==Opening){handState=Turning;handFile=1;handFrame=0;}
                else handFrame=8*256-1;
            }
            if(handState==Normal&&now>handLastEvent+5000){handState=Opening;handFile=0;handFrame=0;}
        }
        if(now>handStep+400)handStep=now; // after a long pause (a loading screen) no catching up
        static const char* files[]={"cursor\\ohand","cursor\\orotate","cursor\\ppress","cursor\\protate"};
        const char* file=files[handFile];
        const uint32_t index=uint32_t(std::clamp(handFrame>>8,0,7));
        const auto* image=uiAssets->frame(file,index);if(!image)return;
        const auto at=uiAssets->offset(file,index);
        d2::HudSprite s;s.image=image;s.w=image->width*scale;s.h=image->height*scale;s.x=mouseX+at[0]*scale;s.y=mouseY+(at[1]-float(image->height))*scale;
        commands.clear();appendSprites({s});gpu->draw(commands);
    }
    // The brightness setting (the game's gamma): the whole picture scaled 0.6x-1.4x, 5 leaving it as drawn.
    void drawBrightness() {
        const int level=touch::settings().brightness;
        if(level==5||!width||!height)return;
        const float scale=.6f+.08f*float(level);const uint8_t grey=uint8_t(std::clamp(scale*127.5f,0.f,255.f));
        d2::HudSprite s;s.x=0;s.y=0;s.w=float(width);s.h=float(height);s.color={grey,grey,grey};s.blend=d2::HudSprite::Blend::Scale;
        commands.clear();appendSprites({s});gpu->draw(commands);
    }
    void present(){drawBrightness();drawCursor();gpu->present();}
    size_t readback(const std::filesystem::path& capture){return gpu->readback(capture);}
};
