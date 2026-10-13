// The interface: control panel, panels (inventory, character, skills, store, stash, cube, waypoints), NPC menus, labels,
// the automap, the game menu.
#include "Renderer.h"
#include "NpcTopics.h"
#include "NativeProtocol.h"
#include <optional>

// The interface is built as a list of sprites (Hud.h) in window pixels; nothing here draws. The helpers below stand in
// for the renderer's texture helpers: a picture (icon, dimIcon, text) is handed on as the picture itself, a plain colour
// (solid) as its colour.
// Where D2Client 1.10f puts an item's sockets (its table at 6FB6EFD0, used by 6FAE0DC0): entry 4*width + height - 5 +
// 8*(sockets - 1) for an item of 1..2 by 1..4 cells, six (x, y) places in half cells: a socket's left edge is (x - 1) / 2
// cells from the item's left, its bottom (y + 1) / 2 cells below the item's top.
static constexpr int8_t socketPlaces[48][6][2]={
    {{1,1},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,2},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,3},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,4},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,1},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,2},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,3},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,4},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{1,3},{0,0},{0,0},{0,0},{0,0}},
    {{1,2},{1,4},{0,0},{0,0},{0,0},{0,0}},
    {{1,3},{1,5},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{3,1},{0,0},{0,0},{0,0},{0,0}},
    {{2,1},{2,3},{0,0},{0,0},{0,0},{0,0}},
    {{2,2},{2,4},{0,0},{0,0},{0,0},{0,0}},
    {{2,2},{2,6},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{1,3},{1,5},{0,0},{0,0},{0,0}},
    {{1,2},{1,4},{1,6},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{3,1},{2,3},{0,0},{0,0},{0,0}},
    {{2,1},{2,3},{2,5},{0,0},{0,0},{0,0}},
    {{2,2},{2,4},{2,6},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{1,3},{1,5},{1,7},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{3,3},{1,3},{3,1},{0,0},{0,0}},
    {{1,2},{3,4},{1,4},{3,2},{0,0},{0,0}},
    {{2,1},{2,3},{2,5},{2,7},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,3},{1,1},{3,5},{1,5},{3,1},{0,0}},
    {{2,4},{1,2},{3,6},{1,6},{3,2},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{1,3},{1,5},{3,1},{3,3},{3,5}},
    {{1,2},{1,4},{1,6},{3,2},{3,4},{3,6}}};

void Renderer::buildHud(std::vector<d2::HudSprite>& hud) {
        struct HudText {const d2::Image* image;uint32_t w,h;};
        auto icon=[&](const d2::Image& image){return HudText{&image,image.width,image.height};};
        auto dimIcon=[&](const d2::Image& image){const auto& dim=dimmedImage(image);return HudText{&dim,dim.width,dim.height};};
        auto text=[&](const std::string& s,uint8_t r,uint8_t g,uint8_t b){const auto& image=textImage(s,r,g,b);return HudText{&image,image.width,image.height};};
        auto solid=[&](uint8_t r,uint8_t g,uint8_t b){return d2::HudPaint(std::array<uint8_t,3>{r,g,b});};
        auto ui=[&](float x,float y,float w,float h,d2::HudPaint paint) {
            d2::HudSprite s;s.image=paint.image;s.color=paint.color;s.x=x;s.y=y;s.w=w;s.h=h;hud.push_back(s);
        };
        // A see-through black box behind text (item names, tooltips), as the game draws them.
        auto shade=[&](float x,float y,float w,float h) {
            d2::HudSprite s;s.x=x;s.y=y;s.w=w;s.h=h;s.blend=d2::HudSprite::Blend::Mix;s.amount=.7f;hud.push_back(s);
        };
        auto label=[&](const std::string& s,float x,float y,uint8_t r,uint8_t g,uint8_t b,bool centered=false) {
            const auto t=text(s,r,g,b);
            const float left=centered?x-t.w*.5f:x;
            ui(left+1,y+1,float(t.w),float(t.h),text(s,0,0,0).image);
            ui(left,y,float(t.w),float(t.h),t.image);
        };
        auto bar=[&](float x,float y,float w,float h,float fraction,std::array<uint8_t,3> fill) {
            ui(x-2,y-2,w+4,h+4,solid(10,10,10));ui(x,y,w,h,solid(45,45,45));
            fraction=std::clamp(fraction,0.f,1.f);
            if(fraction>0)ui(x,y,std::max(1.f,w*fraction),h,solid(fill[0],fill[1],fill[2]));
        };
        // Automap: the player reveals the map cells around them as they go. With Tab the level as seen so far is drawn over
        // the game, a tenth of its size and centred on the player: each tile's picture from AutoMap.txt (ui\automap\maximap,
        // bottom-aligned on the tile's diamond), objects' icons (objects.txt AutoMap), the player, and the area's name.
        if(scene&&walkGrid&&levelTable&&automapTable&&uiAssets&&idleAnimation) {
            revealAround(view.x,view.y);
            const auto& seen=sceneSeen;
            const auto* here=levelTable->byId(int32_t(worldView.level));
            if(automapOpen&&here) {
                const float hudScaleMap=std::min(float(width)/800.f,float(height)/600.f),s=hudScaleMap/10,cx=width*.5f,cy=height*.5f;
                const auto me=d2::projectPoint(*scene,view.x,view.y);
                // Each cell is drawn with its own level's automap pieces, and only cells of the map the player is on.
                const uint32_t map=automapKey(int32_t(worldView.level));
                int16_t lastLevel=-1;const d2::LevelInfo* cellInfo=here;bool sameMap=true;
                for(const auto& p:scene->placements) {
                    if(p.shadow||p.cellX>=scene->cellsX||p.cellY>=scene->cellsY)continue;
                    const size_t at=size_t(p.cellY)*scene->cellsX+p.cellX;
                    if(!seen[at])continue;
                    if(sceneCellLevel[at]!=lastLevel) {
                        lastLevel=sceneCellLevel[at];sameMap=automapKey(lastLevel)==map;
                        cellInfo=levelTable->byId(lastLevel);if(!cellInfo)cellInfo=here;
                    }
                    if(!sameMap)continue;
                    const auto& t=scene->tiles[p.tile];
                    const int frame=automapTable->frame(cellInfo->automapName,t.orientation,t.mainIndex,t.subIndex,(int(p.cellX)+sceneOrigin.x/5)*7+(int(p.cellY)+sceneOrigin.y/5)*13); // variant picked by world cell
                    if(frame<0)continue;
                    const auto* image=uiAssets->frame("automap\\maximap",uint32_t(frame));if(!image)continue;
                    const auto top=d2::projectPoint(*scene,p.cellX*5.f,p.cellY*5.f); // the tile's top corner
                    const float left=cx+(top[0]-80-me[0])*s,bottom=cy+(top[1]+80-me[1])*s;
                    ui(left,bottom-image->height*hudScaleMap,image->width*hudScaleMap,image->height*hudScaleMap,icon(*image).image);
                }
                for(const auto& o:others) {
                    if(o.kind!=5||!objectTable)continue;
                    const auto* info=objectTable->find(o.classId);if(!info||info->automap<=0)continue;
                    const int x=int(std::floor(o.x/5)),y=int(std::floor(o.y/5));
                    if(x<0||y<0||x>=int(scene->cellsX)||y>=int(scene->cellsY)||!seen[size_t(y)*scene->cellsX+x])continue;
                    const auto* image=uiAssets->frame("automap\\maximap",uint32_t(info->automap));if(!image)continue;
                    const auto at=d2::projectPoint(*scene,o.x,o.y);
                    ui(cx+(at[0]-me[0])*s-image->width*hudScaleMap*.5f,cy+(at[1]-me[1])*s-image->height*hudScaleMap,image->width*hudScaleMap,image->height*hudScaleMap,icon(*image).image);
                }
                // The player: a small cross.
                const float arm=3*hudScaleMap,thick=std::max(1.f,hudScaleMap);
                ui(cx-arm,cy-thick*.5f,arm*2,thick,solid(255,255,255));ui(cx-thick*.5f,cy-arm,thick,arm*2,solid(255,255,255));
                // The area's name in the top right corner.
                const auto& name=text(here->name,199,179,119);
                ui(float(width)-float(name.w)-16*hudScaleMap,16*hudScaleMap,float(name.w),float(name.h),name.image);
            }
        }
        // Until IFLAG_IDENTIFIED, magic through crafted keep the base name and the base inventory picture. Identifying
        // reveals the affix, rare, set or unique name and items.txt uniqueinvfile / setinvfile.
        auto itemUnidentified=[](const d2::ItemSnapshot& item){return (item.flags&0x10)==0&&item.quality>=4&&item.quality<=8;};
        auto shownQuality=[&](const d2::ItemSnapshot& item){return itemUnidentified(item)?2u:item.quality;};
        auto shownName=[&](const d2::ItemSnapshot& item){
            if(!itemTable)return std::string();
            if(itemUnidentified(item))return itemTable->displayName(item.code,2,0,0,0,0,0);
            return itemTable->displayName(item.code,item.quality,item.prefix,item.suffix,item.rarePrefix,item.rareSuffix,item.fileIndex);
        };
        // What units say over their heads (packet 0x27, type 3), in white over a see-through box above them.
        std::erase_if(overheadTexts,[](const OverheadText& o){return worldView.tick>=o.until;});
        if(scene)for(const auto& o:overheadTexts)for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(o.guid)) {
            const auto p=d2::projectPoint(*scene,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne);
            const float cx=width*.5f+(p[0]-scene->width*.5f)*zoom+panX,top=height*.5f+(p[1]-scene->height*.5f)*zoom+panY-110*zoom;
            const auto t=text(o.text,255,255,255);
            shade(cx-t.w*.5f-4,top-2,float(t.w)+8,float(t.h)+4);
            label(o.text,cx,top,255,255,255,true);
        }
        // Ground items: Diablo II shows these on the item-display key (Alt). Fresh drops pulse briefly, then hide again.
        itemLabelBoxes.clear();
        const bool altGroundItems=gameWindow&&gameWindow->keyDown(d2::key::Alt);
        if((altGroundItems||!itemLabelUntil.empty())&&itemTable&&strings&&scene) {
            struct GroundLabel {const d2::Image* image=nullptr;float x=0,y=0,w=0,h=0,cx=0,cy=0;int32_t id=0;};
            std::vector<GroundLabel> labels;
            for(const auto& item:worldView.items) {
                if(item.mode!=3&&item.mode!=5)continue;
                if(!altGroundItems&&!itemLabelUntil.count(item.id))continue;
                const auto pos=d2::projectSubtile(*scene,{item.x,item.y});
                const float cx=width*.5f+(pos[0]-scene->width*.5f)*zoom+panX,cy=height*.5f+(pos[1]-scene->height*.5f)*zoom+panY;
                if(cx<-200||cx>width+200||cy<-40||cy>height+40)continue;
                const auto color=itemColor(item);
                const std::string name=shownName(item);
                const auto& t=text(name,color[0],color[1],color[2]);
                labels.push_back({t.image,cx-t.w*.5f,cy-t.h-14,float(t.w),float(t.h),cx,cy,int32_t(item.id)});
            }
            std::sort(labels.begin(),labels.end(),[](const GroundLabel& a,const GroundLabel& b){return a.cy==b.cy?a.cx<b.cx:a.cy<b.cy;});
            std::vector<std::array<float,4>> placed;
            auto overlaps=[](const std::array<float,4>& a,const std::array<float,4>& b) {
                return a[0]<b[2]&&a[2]>b[0]&&a[1]<b[3]&&a[3]>b[1];
            };
            for(auto& label:labels) {
                const float baseY=label.y;
                int best=0;
                for(int n=0;n<18;++n) {
                    const int lane=n==0?0:(n%2?-(n+1)/2:(n+1)/2);
                    const float y=std::clamp(baseY+lane*(label.h+5.f),4.f,float(height)-label.h-4.f);
                    const float x=std::clamp(label.x,4.f,float(width)-label.w-4.f);
                    std::array<float,4> box{x-3,y-2,x+label.w+3,y+label.h+3};
                    bool hit=false;
                    for(const auto& other:placed)if(overlaps(box,other)){hit=true;break;}
                    if(!hit){label.x=x;label.y=y;best=n;break;}
                    best=n;
                }
                if(best==17)label.y=std::clamp(baseY+9*(label.h+5.f),4.f,float(height)-label.h-4.f);
                const std::array<float,4> box{label.x-3,label.y-2,label.x+label.w+3,label.y+label.h+3};
                placed.push_back(box);
                shade(label.x-4,label.y-2,label.w+8,label.h+4);
                ui(label.x,label.y,label.w,label.h,label.image);
                const float left=std::min(label.x-12,label.cx-22),top=std::min(label.y-8,label.cy-24);
                const float right=std::max(label.x+label.w+12,label.cx+22),bottom=std::max(label.y+label.h+8,label.cy+18);
                itemLabelBoxes.push_back({left,top,right-left,bottom-top,label.id});
            }
        }
        // Fit the game's 800x600 layout into the safe area: the control panel centred, side panels at the safe edges,
        // and the HUD bottom-aligned. The game world still renders across the full window.
        const int windowWidth=int(width),windowHeight=int(height);
        int safeX=0,safeY=0,safeW=windowWidth,safeH=windowHeight;
        if(gameWindow)gameWindow->safeArea(safeX,safeY,safeW,safeH);
        safeX=std::clamp(safeX,0,windowWidth);safeY=std::clamp(safeY,0,windowHeight);
        const int safeRight=std::clamp(safeX+safeW,safeX,windowWidth),safeBottom=std::clamp(safeY+safeH,safeY,windowHeight);
        safeW=safeRight-safeX;safeH=safeBottom-safeY;
        if(safeW<=0||safeH<=0){safeX=0;safeY=0;safeW=width;safeH=height;}
        const float safeLeft=float(safeX),safeTop=float(safeY),safeWidth=float(safeW),safeHeight=float(safeH);
        const float hudScale=std::min(safeWidth/800.f,safeHeight/600.f);
        auto hx=[&](float x){return safeLeft+(safeWidth-800.f*hudScale)*.5f+x*hudScale;};
        auto lx=[&](float x){return safeLeft+x*hudScale;};
        auto rx=[&](float x){return safeLeft+safeWidth-(800.f-x)*hudScale;};
        auto hy=[&](float y){return safeTop+safeHeight-(600.f-y)*hudScale;};
        auto sprite=[&](const d2::Image* image,float x,float y){if(image)ui(hx(x),hy(y),image->width*hudScale,image->height*hudScale,icon(*image).image);};
        auto mixedSprite=[&](const d2::Image* image,float x,float y,float amount){
            if(!image)return;
            d2::HudSprite s;s.image=image;s.x=hx(x);s.y=hy(y);s.w=image->width*hudScale;s.h=image->height*hudScale;s.blend=d2::HudSprite::Blend::Mix;s.amount=amount;hud.push_back(s);
        };
        auto hudRegion=[&](float x,float y,float w,float h,int kind){skillRegions.push_back({hx(x),hy(y),w*hudScale,h*hudScale,kind,0,0,0});};
        // Drawing on a panel side: window x of a panel pixel, a picture at its size, and text in the game's font.
        auto sideX=[&](bool right,float x){return right?rx(x):lx(x);};
        auto put=[&](const d2::Image* image,bool right,float x,float y){if(image)ui(sideX(right,x),hy(y),image->width*hudScale,image->height*hudScale,icon(*image).image);};
        auto write=[&](const std::string& s,std::array<uint8_t,3> color,bool right,float x,float y,int align=0,int size=0) {
            // size 0: font16, 1: font8, 2: font6.
            if(!text16||s.empty())return;
            auto& cache=size==2&&text6?*text6:size>=1&&text8?*text8:*text16;
            const auto& image=cache.get(s,color,true);
            put(&image,right,align==0?x:align==1?x-image.width*.5f:x-float(image.width),y);
        };
        auto modeCaption=[&](const std::string& caption,float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,uint8_t br,uint8_t bg,uint8_t bb) {
            ui(x,y,w,h,solid(br,bg,bb));
            const auto t=text(caption,r,g,b);
            if(!t.w||!t.h)return;
            const float textScale=std::min(std::max(1.35f,hudScale*1.15f),std::max(1.f,(w-12.f)/float(t.w)));
            const float tw=float(t.w)*textScale,th=float(t.h)*textScale;
            ui(x+(w-tw)*.5f+textScale,y+(h-th)*.5f+textScale,tw,th,text(caption,0,0,0).image);
            ui(x+(w-tw)*.5f,y+(h-th)*.5f,tw,th,t.image);
        };
        const std::array<uint8_t,3> white{255,255,255},gold{199,179,119},grey{120,120,120},red{255,77,77};
        const auto& vitals=worldView.vitals;
        skillRegions.clear();
        // Which panel is up on each side: store, waypoints or character on the left; inventory or skill tree on the right.
        const bool storageUp=(stashOpen||cubeOpen)&&itemTable&&itemArt&&uiAssets; // the stash or the Horadric Cube
        const bool storeUp=!storageUp&&npcDialog.open&&npcDialog.page==NpcDialog::Page::Shop&&itemTable;
        const bool waypointUp=!storageUp&&!storeUp&&waypointMenu.open&&levelTable;
        const bool characterUp=!storageUp&&!storeUp&&!waypointUp&&characterOpen&&skillTable;
        const bool questUp=!storageUp&&!storeUp&&!waypointUp&&!characterUp&&questLog.open&&uiAssets&&strings;
        const bool hirelingUp=!storageUp&&!storeUp&&!waypointUp&&!characterUp&&!questUp&&hirelingOpen&&worldView.hireling.guid&&uiAssets&&strings&&itemTable&&itemArt;
        const bool inventoryUp=inventoryOpen&&itemTable&&itemArt;
        const bool treeUp=!inventoryUp&&skillsOpen&&skillTable&&skillArt;
        if(gamePetTypes&&uiAssets&&!pets.empty()&&!storageUp&&!storeUp&&!waypointUp&&!characterUp&&!questUp&&!hirelingUp) {
            // The pets' roster (D2Client 0x6FADAB00): from the top left, 56 pixels apart from x 15, each kind's icon
            // (ui\hireables\<pettype.txt icon>) standing on y 60, its life above it (y 14-19, 46 wide: green, yellow under half,
            // red under a quarter) and its name below (font6, at most 66 wide); a kind shown once with its number.
            auto ty=[&](float y){return safeTop+y*hudScale;};
            float x=15;
            std::map<uint32_t,int> counted;
            for(const auto& pet:pets)if(const auto* info=gamePetTypes->find(pet.type);info&&info->iconType==2)++counted[pet.type];
            std::set<uint32_t> shownKinds;
            for(const auto& pet:pets) {
                const auto* info=gamePetTypes->find(pet.type);
                if(!info||info->baseIcon.empty())continue; // no portrait (invisible pets, traps)
                if(info->iconType==2&&!shownKinds.insert(pet.type).second)continue;
                const auto* image=uiAssets->frame("hireables\\"+gamePetTypes->icon(pet.type,pet.classId),0);
                if(image)ui(lx(x),ty(60.f-float(image->height)),image->width*hudScale,image->height*hudScale,icon(*image).image);
                if(image&&pet.guid==mercGuid)skillRegions.push_back({lx(x),ty(60.f-float(image->height)),image->width*hudScale,image->height*hudScale,47,0,0,0}); // opens its panel
                if(info->drawHp)for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(pet.guid)&&e.maxLife>0) {
                    const int pct=std::clamp(int(int64_t(e.life)*100/e.maxLife),0,100),filled=pct*46/100;
                    const auto color=pct<25?solid(255,0,0):pct<50?solid(192,192,0):solid(0,128,0);
                    if(filled>0)ui(lx(x),ty(14),float(filled)*hudScale,5*hudScale,color);
                    if(filled<46)ui(lx(x+float(filled)),ty(14),float(46-filled)*hudScale,5*hudScale,solid(0,0,0));
                }
                std::string caption=pet.name&&strings?strings->byId(pet.name):strings&&!info->name.empty()?strings->get(info->name):std::string();
                if(info->iconType==2)caption=std::to_string(counted[pet.type]);
                if(!caption.empty()&&text6) {
                    const auto& image=text6->get(caption,white,true);
                    const float w=std::min(66.f,float(image.width));
                    ui(lx(x+23-w*.5f),ty(64),w*hudScale,image.height*hudScale,icon(image).image);
                }
                x+=56;
            }
        }
        auto panelArt=[&](const std::string& file,uint32_t first,bool right) {
            // A panel is four frames (256/64 wide, 256/176 tall) at (80,60) on the left or (400,60) on the right.
            static const float px[]={0,256,0,256},py[]={0,0,256,256};
            for(uint32_t k=0;k<4;++k)put(uiAssets->frame(file,first+k),right,(right?400.f:80.f)+px[k],60.f+py[k]);
        };
        if(uiAssets) {
            // The carved frame around an open panel (800borderframe: 0-4 the left side, 5-9 the right side). Drawn first: the control panel covers its foot.
            static const float frameX[]={0,256,0,0,256,399,544,713,544,399},frameY[]={0,0,256,487,487,0,0,256,487,487};
            for(int side=0;side<2;++side)if(side?(inventoryUp||treeUp):(storageUp||storeUp||waypointUp||characterUp||questUp))
                for(uint32_t f=0;f<5;++f)put(uiAssets->frame("panel\\800borderframe",f+5*side),side==1,frameX[f+5*side],frameY[f+5*side]);
        }
        if(uiAssets) {
            // Pieces of 800ctrlpnl7: globe holders, the level-up boxes, stamina and experience, belt; the skills sit in the 48 pixel gaps.
            static const float pieceX[]={0,165,293,421,549,683};
            for(uint32_t i=0;i<6;++i)if(const auto* piece=uiAssets->frame("panel\\800ctrlpnl7",i))sprite(piece,pieceX[i],600.f-piece->height);
            const auto& st=worldView.stats;
            auto fixedFill=[&](int currentStat,int maxStat,int current,int maximum) {
                if(maxStat>0)return std::clamp(float(std::max(0,currentStat))/float(maxStat),0.f,1.f);
                return maximum>0?std::clamp(float(std::max(0,current))/float(maximum),0.f,1.f):0.f;
            };
            const float lifeFill=fixedFill(st[6],st[7],vitals.life,vitals.maxLife),manaFill=fixedFill(st[8],st[9],vitals.mana,vitals.maxMana);
            const float potionLifeFill=fixedFill(vitals.potionLife,st[7],vitals.life,vitals.maxLife),potionManaFill=fixedFill(vitals.potionMana,st[9],vitals.mana,vitals.maxMana);
            // Globes (hlthmana: 0 life, 1 mana) filled to the current level, under their glass (overlap).
            if(potionLifeFill>lifeFill+.01f)mixedSprite(uiAssets->filled("panel\\hlthmana",0,potionLifeFill),30,511,.4f);
            if(potionManaFill>manaFill+.01f)mixedSprite(uiAssets->filled("panel\\hlthmana",1,potionManaFill),690,511,.4f);
            sprite(uiAssets->filled("panel\\hlthmana",0,lifeFill),30,511);
            sprite(uiAssets->filled("panel\\hlthmana",1,manaFill),690,511);
            sprite(uiAssets->frame("panel\\overlap",0),28,507);
            sprite(uiAssets->frame("panel\\overlap",1),690,507);
            // Stamina (yellow, red when low) and experience within the current level.
            const float stamina=st[11]>0?std::clamp(float(st[10])/float(st[11]),0.f,1.f):0;
            if(stamina>0)ui(hx(273),hy(571),102*stamina*hudScale,19*hudScale,stamina<.25f?solid(190,40,30):solid(200,160,50));
            const int level=st[12];
            const int64_t levelStart=level>=2&&size_t(level-2)<levelExperience.size()?levelExperience[size_t(level-2)]:0,levelEnd=int64_t(uint32_t(st[30]));
            const float experience=level<99&&levelEnd>levelStart?std::clamp(float(int64_t(uint32_t(st[13]))-levelStart)/float(levelEnd-levelStart),0.f,1.f):0; // none at the last level (99)
            if(experience>0)ui(hx(256),hy(560),120*experience*hudScale,4*hudScale,solid(216,196,120));
            sprite(uiAssets->frame("panel\\runbutton",runEnabled?2:0),255,571);
            hudRegion(255,571,16,20,37);
            // Level-up buttons in the two boxes with a cross: lit while there are points to spend.
            if(st[4]>0){sprite(uiAssets->frame("panel\\level",0),206,562);hudRegion(206,562,30,30,38);}
            if(st[5]>0){sprite(uiAssets->frame("panel\\level",0),563,562);hudRegion(563,562,30,30,39);}
            // The arrow in the middle (menubutton: 0/1 closed, 2/3 open, the odd frames pressed) opens the mini panel above it: eight
            // buttons (minipanelbtn, normal and pressed in pairs) on the minipanel background.
            const bool pressing=leftButtonDown;
            auto under=[&](float x,float y,float w,float h){return mouseX>=hx(x)&&mouseX<hx(x+w)&&mouseY>=hy(y)&&mouseY<hy(y+h);};
            sprite(uiAssets->frame("panel\\menubutton",(miniPanelOpen?2:0)+(pressing&&under(392,562,15,24)?1:0)),392,562);
            hudRegion(392,562,15,24,44);
            if(miniPanelOpen) {
                constexpr float left=312,top=527;
                sprite(uiAssets->frame("panel\\minipanel",0),left,top);
                for(int i=0;i<8;++i) {
                    const float x=left+3+i*21.f,y=top+3;
                    sprite(uiAssets->frame("panel\\minipanelbtn",uint32_t(i*2+(pressing&&under(x,y,20,20)?1:0))),x,y);
                    skillRegions.push_back({hx(x),hy(y),20*hudScale,20*hudScale,45,i,0,0});
                }
                hudRegion(left,top,176,26,36);
            }
            // Clicks on the panel itself do not walk.
            hudRegion(0,496,117,104,36);hudRegion(117,545,683-117,55,36);hudRegion(683,496,117,104,36);
        }
        // Inventory panel: equipment slots and the 10 by 4 grid, item pictures from the game's inventory graphics.
        inventoryRegions.clear();goldRegions.clear();cursorItem=0;cursorItemW=cursorItemH=1;
        auto tint=[](uint32_t quality)->std::array<uint8_t,3> {
            switch(quality){case 4:return {30,30,90};case 5:return {20,80,20};case 6:return {80,75,20};case 7:return {85,60,20};case 8:return {90,55,15};default:return {50,46,42};}
        };
        auto backing=[&](const d2::ItemSnapshot& item)->std::array<uint8_t,3> {
            return itemUnidentified(item)?std::array<uint8_t,3>{120,20,20}:tint(item.quality);
        };
        // The box D2Client 1.10f fills behind an item (D2GFX_DrawBox, draw mode 0 = 25%), in the colours it makes at start-up
        // (6FADD2F0): red 128,0,0 when the player can't use it (D2Common ITEMS_CheckRequirements), when it is broken or not
        // equippable, or is the armed identify scroll (IFLAG_TARGETING); 128,64,64 when unidentified; otherwise blue 0,0,128
        // in a grid (backpack, stash, cube, store: 6FAE1180) and nothing on the body (6FAE0512).
        // On the body, a bow or crossbow (it uses ammo) or a quiver with the other hand empty is red too (6FAE0512).
        auto originalBacking=[&](const d2::ItemSnapshot& item,bool worn,bool otherHandEmpty)->std::optional<std::array<uint8_t,3>> {
            constexpr uint32_t targeting=0x4,identified=0x10,broken=0x100,noEquip=0x4000;
            if(worn&&otherHandEmpty&&(item.traits&(d2::native::itemTraitUsesAmmo|d2::native::itemTraitQuiver)))return std::array<uint8_t,3>{128,0,0};
            if(!(item.traits&d2::native::itemTraitUsable)||(item.flags&(broken|noEquip))||(!worn&&(item.flags&targeting)))return std::array<uint8_t,3>{128,0,0};
            if(!(item.flags&identified))return std::array<uint8_t,3>{128,64,64};
            if(worn)return std::nullopt;
            return std::array<uint8_t,3>{0,0,128};
        };
        auto drawIcon=[&](const d2::ItemSnapshot& item,float x,float y,float w,float h,bool centered) {
            const auto* image=itemArt?itemArt->get(item.code,shownQuality(item),item.invGraphic,item.invTrans,item.invColor):nullptr;
            if(!image)return;
            const auto& t=icon(*image);
            if(centered)ui(x+(w-t.w)*.5f,y+(h-t.h)*.5f,float(t.w),float(t.h),t.image);
            else ui(x,y,float(t.w),float(t.h),t.image);
        };
        for(const auto& item:worldView.items) {
            if(item.mode!=4)continue;
            cursorItem=int32_t(item.id);
            if(const auto* info=itemTable?itemTable->find(item.code):nullptr){cursorItemW=info->invWidth;cursorItemH=info->invHeight;}
        }
        // An item picture centred in a box given in panel pixels, at the interface scale.
        // The sockets of the identified item under the mouse (D2Client 6FAE0DC0), from (x, y), the top left of its picture:
        // panel\GemSocket at each place (its left a pixel to the left, its bottom on the place) and, over it, the picture of
        // what fills it (top left a cell above the place), in the order the sockets were filled.
        auto drawSockets=[&](const d2::ItemSnapshot& item,bool right,float x,float y) {
            const auto* info=itemTable?itemTable->find(item.code):nullptr;
            if(!(item.flags&0x10)||item.sockets==0||!info||!uiAssets||info->invWidth<1||info->invWidth>2||info->invHeight<1||info->invHeight>4)return;
            constexpr float cell=29;
            const int n=int(std::min<uint32_t>(item.sockets,6));
            const auto& places=socketPlaces[4*info->invWidth+info->invHeight-5+8*(n-1)];
            std::array<const d2::ItemSnapshot*,6> filled{};
            for(const auto& inside:worldView.items)if(inside.mode==6&&inside.parent==item.id&&inside.x>=0&&inside.x<n)filled[size_t(inside.x)]=&inside;
            const auto* socket=uiAssets->frame("panel\\gemsocket",0);
            for(int i=0;i<n;++i) {
                const float sx=x+float((places[i][0]-1)*int(cell)/2),bottom=y+float((places[i][1]+1)*int(cell)/2);
                if(socket)put(socket,right,sx-1,bottom-float(socket->height));
                if(filled[size_t(i)])if(const auto* gem=itemArt?itemArt->get(filled[size_t(i)]->code,shownQuality(*filled[size_t(i)]),filled[size_t(i)]->invGraphic,filled[size_t(i)]->invTrans,filled[size_t(i)]->invColor):nullptr)put(gem,right,sx,bottom-cell);
            }
        };
        auto putItem=[&](const d2::ItemSnapshot& item,bool right,float x,float y,float w,float h,bool worn=false,bool otherHandEmpty=false) {
            const auto* image=itemArt?itemArt->get(item.code,shownQuality(item),item.invGraphic,item.invTrans,item.invColor):nullptr;
            // The item under the mouse with nothing held: a green 25% box instead of its usual one, and its sockets (D2Client
            // 6FAE10F6 for grids, 6FAE047A for the body).
            const float sx=sideX(right,x),sy=hy(y);
            const bool hovered=!cursorItem&&mouseX>=sx&&mouseX<=sx+w*hudScale&&mouseY>=sy&&mouseY<=sy+h*hudScale;
            if(const auto c=hovered?std::optional<std::array<uint8_t,3>>{{0,128,0}}:originalBacking(item,worn,otherHandEmpty)) {
                d2::HudSprite bg;bg.x=sx;bg.y=sy;bg.w=w*hudScale;bg.h=h*hudScale;bg.color=*c;bg.blend=d2::HudSprite::Blend::Mix;bg.amount=.25f;hud.push_back(bg);
            }
            const float ix=image?x+(w-float(image->width))*.5f:x,iy=image?y+(h-float(image->height))*.5f:y;
            if(image)put(image,right,ix,iy);
            if(hovered)drawSockets(item,right,ix,iy);
            // Stacks (books, throwing weapons) draw their count in font6 at the cell's lower right. Gold's quantity is the
            // pile's value, which the tooltip already shows, so it stays off the picture. Scrolls have no max stack.
            if(const auto* info=itemTable?itemTable->find(item.code):nullptr;info&&info->type!="gold"&&item.quantity>0&&(info->maxStack>1||item.quantity>1))
                write(std::to_string(item.quantity),white,right,x+w-2,y+h-8,2,2);
        };
        // The item held over a grid (D2Client 6FAE15D2): where it would go, green when it can be put down there
        // (INVENTORY_CanItemBePlaced), the one item it would swap with or go into (the cube) yellow (128,128,0), and red when
        // it can't (more than one item under it). 25% boxes, cell by cell. `occupied` holds each cell's item (0 empty).
        auto heldPreview=[&](bool right,float gridX,float gridY,int columns,int rows,const int32_t* occupied,int stride) {
            constexpr float cell=29;
            if(!cursorItem)return;
            const float left=sideX(right,gridX),top=hy(gridY);
            const int mx=int(std::floor((float(mouseX)-left)/(cell*hudScale))),my=int(std::floor((float(mouseY)-top)/(cell*hudScale)));
            if(mx<0||my<0||mx>=columns||my>=rows)return;
            // The same top left cell the click uses (Input.cpp putHeldItem).
            const int x=std::clamp(mx-(cursorItemW-1)/2,0,std::max(0,columns-cursorItemW)),y=std::clamp(my-(cursorItemH-1)/2,0,std::max(0,rows-cursorItemH));
            if(x+cursorItemW>columns||y+cursorItemH>rows)return;
            std::vector<int32_t> under;
            for(int dy=0;dy<cursorItemH;++dy)for(int dx=0;dx<cursorItemW;++dx)if(const int32_t id=occupied[(y+dy)*stride+x+dx];id&&std::find(under.begin(),under.end(),id)==under.end())under.push_back(id);
            auto paint=[&](int px,int py,int w,int h,std::array<uint8_t,3> c){
                for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx) {
                    d2::HudSprite box;box.x=left+float(px+dx)*cell*hudScale;box.y=top+float(py+dy)*cell*hudScale;box.w=box.h=cell*hudScale;
                    box.color=c;box.blend=d2::HudSprite::Blend::Mix;box.amount=.25f;hud.push_back(box);
                }
            };
            if(under.empty())paint(x,y,cursorItemW,cursorItemH,{0,128,0});
            else if(under.size()>1)paint(x,y,cursorItemW,cursorItemH,{128,0,0});
            else for(const auto& item:worldView.items)if(int32_t(item.id)==under.front())if(const auto* info=itemTable?itemTable->find(item.code):nullptr)paint(item.x,item.y,info->invWidth,info->invHeight,{128,128,0});
        };
        auto closeButton=[&](bool right,float x,float y,std::vector<InventoryRegion>& regions,int kind,int a) {
            // Keep the original artwork, but give touch input a 48-HUD-pixel target.
            const float sx=sideX(right,x),sy=hy(y),size=32*hudScale;
            const float target=48*hudScale,hitX=sx-(target-size)*.5f,hitY=sy-(target-size)*.5f;
            const bool down=leftHeld&&mouseX>=hitX&&mouseX<=hitX+target&&mouseY>=hitY&&mouseY<=hitY+target;
            put(uiAssets->frame("panel\\buysellbtn",down?11:10),right,x,y);
            regions.push_back({hitX,hitY,target,target,kind,a,0,0});
        };
        if(inventoryUp&&uiAssets) {
            // invchar6 frames 4-7 at (400,60); the slots and the 10 by 4 grid where inventory.txt ("Amazon2" ...) puts them for 800x600.
            panelArt(expansionGame?"panel\\invchar6":"panel\\invchar",4,true); // a classic character's panel has no weapon set tabs
            panelX=rx(400);panelY=hy(60);panelW=320*hudScale;panelH=432*hudScale;
            struct Slot {int loc;float x,y,w,h;};
            static const Slot slots[]={{1,535,68,54,51},{2,609,95,23,24},{3,533,137,56,82},{4,420,107,55,112},{5,651,107,55,112},
                {6,495,240,23,24},{7,609,240,23,24},{8,536,239,52,25},{10,421,241,54,53},{9,652,242,54,52}};
            auto wornAt=[&](int loc)->const d2::ItemSnapshot*{for(const auto& item:worldView.items)if(item.mode==1&&!item.vendor&&item.bodyLoc==uint32_t(loc))return &item;return nullptr;};
            // Weapon set II in use (6FADF64D): invchar6Tab over the weapon boxes, its tab II raised (the panel's own art has I
            // raised): frame 1 at the right hand's box, frame 0 at the left's, 5 left of the box and its bottom 4 below it.
            if(secondWeaponSet&&expansionGame) {
                if(const auto* tab=uiAssets->frame("panel\\invchar6Tab",1))put(tab,true,420-5,219+4-float(tab->height));
                if(const auto* tab=uiAssets->frame("panel\\invchar6Tab",0))put(tab,true,651-5,219+4-float(tab->height));
            }
            for(const auto& slot:slots) {
                const d2::ItemSnapshot* worn=wornAt(slot.loc);
                const bool hand=slot.loc==4||slot.loc==5;
                const d2::ItemSnapshot* otherHand=hand?wornAt(slot.loc==4?5:4):nullptr;
                // An empty hand shows the weapons silhouette (inv_weapons, 6FADF986), its bottom 2 above the box's, moved 4 right
                // and 4 up on set II's box; not beside a two-handed weapon, which is drawn there again (below).
                if(hand&&!worn&&!(otherHand&&worldView.wieldType==2))if(const auto* empty=uiAssets->frame("panel\\inv_weapons",0))
                    put(empty,true,slot.x+(secondWeaponSet?4:0),slot.y+slot.h-2-(secondWeaponSet?4:0)-float(empty->height));
                if(worn)putItem(*worn,true,slot.x,slot.y,slot.w,slot.h,true,hand&&!otherHand);
                // A two-handed weapon in one hand and nothing in the other (INVENTORY_GetWieldType 2): D2Client draws the weapon
                // again in the empty hand's slot with a red 25% box over it (6FAE02B4-6FAE0396), unless it is a quiver.
                if(worn&&hand&&!otherHand&&worldView.wieldType==2&&!(worn->traits&d2::native::itemTraitQuiver)) {
                    for(const auto& other:slots)if(other.loc==(slot.loc==4?5:4))if(const auto* image=itemArt?itemArt->get(worn->code,shownQuality(*worn),worn->invGraphic,worn->invTrans,worn->invColor):nullptr) {
                        put(image,true,other.x+(other.w-float(image->width))*.5f,other.y+(other.h-float(image->height))*.5f);
                        d2::HudSprite box;box.x=rx(other.x);box.y=hy(other.y);box.w=other.w*hudScale;box.h=other.h*hudScale;box.color={128,0,0};box.blend=d2::HudSprite::Blend::Mix;box.amount=.25f;hud.push_back(box);
                    }
                }
                inventoryRegions.push_back({rx(slot.x),hy(slot.y),slot.w*hudScale,slot.h*hudScale,2,slot.loc,0,worn?int32_t(worn->id):0});
            }
            // An item held over a body slot (6FAE070F): green when it may go there (ITEMS_CheckBodyLocation) and the player
            // can use it, or when it fits a free socket of the item there; red otherwise. Drawn over the slot, 25%.
            if(const auto* held=cursorItem?[&]()->const d2::ItemSnapshot*{for(const auto& item:worldView.items)if(int32_t(item.id)==cursorItem)return &item;return nullptr;}():nullptr)
                for(const auto& slot:slots)if(mouseX>=rx(slot.x)&&mouseX<=rx(slot.x)+slot.w*hudScale&&mouseY>=hy(slot.y)&&mouseY<=hy(slot.y)+slot.h*hudScale) {
                    const auto* there=wornAt(slot.loc);
                    const bool fits=(worldView.bodyFits&(1u<<slot.loc))&&(held->traits&d2::native::itemTraitUsable);
                    const bool socket=there&&(held->traits&d2::native::itemTraitSocketFiller)&&(there->traits&d2::native::itemTraitTakesFiller);
                    d2::HudSprite box;box.x=rx(slot.x);box.y=hy(slot.y);box.w=slot.w*hudScale;box.h=slot.h*hudScale;
                    box.color=fits||socket?std::array<uint8_t,3>{0,128,0}:std::array<uint8_t,3>{128,0,0};box.blend=d2::HudSprite::Blend::Mix;box.amount=.25f;hud.push_back(box);
                }
            constexpr float cell=29,gridX=419,gridY=315;
            auto& occupied=inventoryOccupied;std::memset(occupied,0,sizeof(occupied));
            for(const auto& item:worldView.items) {
                if(item.mode!=0||item.page!=0||item.vendor)continue;
                const auto* info=itemTable->find(item.code);if(!info)continue;
                for(int dy=0;dy<info->invHeight;++dy)for(int dx=0;dx<info->invWidth;++dx)if(item.x+dx>=0&&item.x+dx<10&&item.y+dy>=0&&item.y+dy<4)occupied[item.y+dy][item.x+dx]=int32_t(item.id);
                putItem(item,true,gridX+item.x*cell,gridY+item.y*cell,info->invWidth*cell,info->invHeight*cell);
            }
            heldPreview(true,gridX,gridY,10,4,&occupied[0][0],10);
            for(int cy=0;cy<4;++cy)for(int cx=0;cx<10;++cx)inventoryRegions.push_back({rx(gridX+cx*cell),hy(gridY+cy*cell),cell*hudScale,cell*hudScale,1,cx,cy,occupied[cy][cx]});
            // The I / II tabs above the two weapon boxes swap weapon sets (as the W key does).
            if(expansionGame)skillRegions.push_back({rx(420),hy(88),55*hudScale,18*hudScale,46,0,0,0});
            if(expansionGame)skillRegions.push_back({rx(651),hy(88),55*hudScale,18*hudScale,46,0,0,0});
            // Gold beside the coin (goldcoinbtn in the small box; it opens the gold popup), and the close button in the corner box.
            {
                const bool down=leftButtonDown&&mouseX>=rx(483)&&mouseX<rx(503)&&mouseY>=hy(453)&&mouseY<hy(471);
                put(uiAssets->frame("panel\\goldcoinbtn",down?1:0),true,483,453);
                goldRegions.push_back({rx(483),hy(453),20*hudScale,18*hudScale,60,0,0,0});
            }
            write(std::to_string(worldView.vitals.gold),white,true,596,452,2);
            closeButton(true,419,445,skillRegions,42,0);
            if(touch::enabled()&&sellToNpc)
                modeCaption("TAP AN ITEM TO SELL",rx(430),hy(296),250*hudScale,18*hudScale,255,90,70,90,28,22);
        }
        // Stash (tradestash, a 6 by 8 grid at 154,142, inventory.txt "Big Bank Page2") or Horadric Cube (supertransmogrifier,
        // a 3 by 4 grid at 198,199, "Transmogrify Box2") on the left, with their items, the stash's gold, the cube's transmute
        // button (miniconvert) and the close button.
        storageRegions.clear();
        if(storageUp) {
            panelArt(stashOpen?"panel\\tradestash":"panel\\supertransmogrifier",0,false);
            storageX=lx(80);storageY=hy(60);storageW=320*hudScale;storageH=432*hudScale;
            storageRegions.push_back({storageX,storageY,storageW,storageH,53,0,0,0});
            constexpr float cell=29;
            const int columns=stashOpen?6:3,rows=stashOpen?8:4;const uint32_t page=stashOpen?4:3;
            const float gridX=stashOpen?154:198,gridY=stashOpen?142:199;
            std::memset(storageOccupied,0,sizeof(storageOccupied));
            for(const auto& item:worldView.items) {
                if(item.mode!=0||item.page!=page||item.vendor)continue;
                const auto* info=itemTable->find(item.code);if(!info)continue;
                for(int dy=0;dy<info->invHeight;++dy)for(int dx=0;dx<info->invWidth;++dx)
                    if(item.x+dx>=0&&item.x+dx<columns&&item.y+dy>=0&&item.y+dy<rows)storageOccupied[item.y+dy][item.x+dx]=int32_t(item.id);
                putItem(item,false,gridX+item.x*cell,gridY+item.y*cell,info->invWidth*cell,info->invHeight*cell);
            }
            heldPreview(false,gridX,gridY,columns,rows,&storageOccupied[0][0],6);
            for(int cy=0;cy<rows;++cy)for(int cx=0;cx<columns;++cx)storageRegions.push_back({lx(gridX+cx*cell),hy(gridY+cy*cell),cell*hudScale,cell*hudScale,50,cx,cy,storageOccupied[cy][cx]});
            if(stashOpen) {
                // The gold kept in the stash, and its coin (withdraws through the gold popup).
                const bool down=leftButtonDown&&mouseX>=lx(153)&&mouseX<lx(173)&&mouseY>=hy(82)&&mouseY<hy(100);
                put(uiAssets->frame("panel\\goldcoinbtn",down?1:0),false,153,82);
                goldRegions.push_back({lx(153),hy(82),20*hudScale,18*hudScale,61,0,0,0});
                write(std::to_string(worldView.stats[15]),white,false,245,85,2);
            } else {
                const bool pressing=leftButtonDown;
                const bool down=pressing&&mouseX>=lx(223)&&mouseX<lx(255)&&mouseY>=hy(318)&&mouseY<hy(350);
                put(uiAssets->frame("panel\\miniconvert",down?1:0),false,223,318);
                storageRegions.push_back({lx(223),hy(318),32*hudScale,32*hudScale,52,0,0,0});
            }
            closeButton(false,351,443,storageRegions,51,0);
        }
        // NPC dialog: the menu, what the NPC says, and the store.
        dialogRegions.clear();
        if(npcDialog.open&&itemTable&&strings) {
            const auto* sprite=unitArt?unitArt->sprite(npcDialog.classId):nullptr;
            std::string npcName=sprite?strings->get(sprite->nameKey):std::string("Stranger");
            if(!npcName.empty())npcName[0]=char(std::toupper(static_cast<unsigned char>(npcName[0])));
            // As the game draws its NPC menus: text in the game's font over a see-through black box, the choice under the cursor in gold.
            auto panel=[&](float x,float y,float w,float h){
                dialogX=x;dialogY=y;dialogW=w;dialogH=h;
                shade(x,y,w,h);
                dialogRegions.push_back({x,y,w,h,23,0,0,0});
            };
            auto button=[&](const std::string& caption,float x,float y,float w,int action){
                const bool hot=mouseX>=x&&mouseX<=x+w&&mouseY>=y&&mouseY<=y+26;
                const auto t=hot?text(caption,199,179,119):text(caption,255,255,255);
                ui(x+w*.5f-t.w*.5f,y+3,float(t.w),float(t.h),t.image);
                dialogRegions.push_back({x,y,w,26,20,action,0,0});
            };
            if(npcDialog.page==NpcDialog::Page::Menu) {
                // Over the NPC's head, as the game puts it: their name, then talk / trade (trade/repair at the smiths) /
                // gamble / cancel in the game's words (strings 3381, 3396, 3334, 3398, 3400).
                const bool trade=npcDialog.services&1,gamble=npcDialog.services&2;
                const uint32_t c=npcDialog.classId;
                const bool cain=c==146||c==244||c==245||c==246||c==265||c==520; // Deckard Cain identifies items
                const bool smith=c==154||c==178||c==253||c==257||c==511;         // Charsi, Fara, Hratli, Halbu, Larzuk repair
                std::vector<std::pair<std::string,int>> options;
                // "Introduction" (3399) above Talk for the NPCs D2Client's table gives one (0x6FAF3F8B).
                if(hasIntroduction(c)){std::string caption=strings->byId(3399);if(!caption.empty())caption[0]=char(std::toupper(static_cast<unsigned char>(caption[0])));options.push_back({caption,9});}
                options.push_back({strings->byId(3381),1});
                // The NPC's quest messages, each under its quest's name (NpcTopics.h), after talk as the original lists them.
                for(size_t t=0;t<npcDialog.topics.size();++t)options.push_back({strings->byId(d2::topicCaption(npcDialog.topics[t])),100+int(t)});
                if(trade)options.push_back({strings->byId(smith?3334:3396),2});
                if(gamble)options.push_back({strings->byId(3398),3});
                if(cain)options.push_back({"Identify Items",6});
                const bool hires=c==150||c==198||c==252||c==515; // Kashya, Greiz, Asheara, Qual-Kehk
                if(hires)options.push_back({strings->byId(3397),7});
                if((hires||c==367)&&deadMercName>=0) {
                    // "Resurrect %s: %d" (22696) with the dead hireling's name and the cost (packet 0x9B).
                    std::string caption=strings->byId(22696);
                    if(const auto at=caption.find("%s");at!=std::string::npos)caption.replace(at,2,strings->byId(uint32_t(deadMercName)));
                    if(const auto at=caption.find("%d");at!=std::string::npos)caption.replace(at,2,std::to_string(resurrectCost));
                    options.push_back({caption,8});
                }
                options.push_back({strings->byId(3400),4});
                const bool largerForTouch=touch::enabled();
                const float preferredScale=std::max(1.f,hudScale);
                const float fitScale=std::max(1.f,float(safeHeight)-16.f)/(60.f+48.f*float(options.size()));
                const float scale=largerForTouch?std::max(.5f,std::min(preferredScale,fitScale)):1.f;
                const float fontScale=largerForTouch?std::max(1.5f,scale*1.25f):1.f;
                const float rowHeight=largerForTouch?48*scale:26;
                const float sidePadding=largerForTouch?18*scale:10;
                const float titleTop=largerForTouch?12*scale:8;
                const float titleHeight=largerForTouch?36*scale:26;
                const float optionsTop=titleTop+titleHeight;
                const float bottomPadding=largerForTouch?12*scale:6;
                const float titleWidth=float(text(npcName,235,215,140).w)*fontScale+2*sidePadding;
                float w=titleWidth;
                for(const auto& [caption,action]:options)
                    w=std::max(w,float(text(caption,255,255,255).w)*fontScale+2*sidePadding);
                const float availableWidth=largerForTouch?std::max(1.f,float(safeWidth)-16.f):std::max(1.f,float(width)-16.f);
                w=std::min(w,availableWidth);
                const float h=titleTop+titleHeight+rowHeight*float(options.size())+bottomPadding;
                float nx=width*.5f,ny=height*.4f;
                for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(npcDialog.guid)&&scene) {
                    const auto p=d2::projectPoint(*scene,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne);
                    nx=width*.5f+(p[0]-scene->width*.5f)*zoom+panX;ny=height*.5f+(p[1]-scene->height*.5f)*zoom+panY;
                }
                const float minX=largerForTouch?safeLeft+8:8;
                const float maxX=largerForTouch?safeLeft+safeWidth-w-8:float(width)-w-8;
                const float x=minX+std::max(0.f,std::min(nx-w*.5f-minX,maxX-minX));
                const float minY=largerForTouch?safeTop+8:8;
                const float maxY=largerForTouch?safeTop+safeHeight-h-8:float(height)-h-8;
                const float y=minY+std::max(0.f,std::min(ny-95*zoom-h-minY,maxY-minY));
                panel(x,y,w,h);
                const auto menuText=[&](const std::string& caption,float centerX,float top,float areaHeight,uint8_t r,uint8_t g,uint8_t b) {
                    const auto t=text(caption,r,g,b);
                    if(!t.w||!t.h)return;
                    const float textScale=std::min(fontScale,std::max(1.f,w-2*sidePadding)/float(t.w));
                    const float textWidth=float(t.w)*textScale,textHeight=float(t.h)*textScale;
                    ui(centerX-textWidth*.5f+textScale,top+(areaHeight-textHeight)*.5f+textScale,textWidth,textHeight,text(caption,0,0,0).image);
                    ui(centerX-textWidth*.5f,top+(areaHeight-textHeight)*.5f,textWidth,textHeight,t.image);
                };
                menuText(npcName,x+w*.5f,y+titleTop,titleHeight,235,215,140);
                float by=y+optionsTop;
                for(const auto& [caption,action]:options) {
                    const bool hot=mouseX>=x&&mouseX<=x+w&&mouseY>=by&&mouseY<=by+rowHeight;
                    if(largerForTouch) {
                        const float inset=2*scale,border=std::max(1.f,scale);
                        ui(x+sidePadding*.5f,by+inset,w-sidePadding,rowHeight-2*inset,
                           solid(hot?220:110,hot?190:92,hot?120:56));
                        ui(x+sidePadding*.5f+border,by+inset+border,w-sidePadding-2*border,rowHeight-2*inset-2*border,
                           solid(hot?74:24,hot?60:21,hot?35:17));
                    }
                    menuText(caption,x+w*.5f,by,rowHeight,hot?199:255,hot?179:255,hot?119:255);
                    dialogRegions.push_back({x,by,w,rowHeight,20,action,0,0});
                    by+=rowHeight;
                }
            } else if(npcDialog.page==NpcDialog::Page::Hire) {
                // The mercenaries on offer, as D2Client 0x6FAF5028 lists them: the name, then "Lvl: 12  Life: 200  Def: 30
                // Cost: 3000" (strings 3368, 3366, 3367, 3369 and ": "), and its special in grey (hireling.txt HireDesc); or
                // "There are no Mercenaries left to hire." (3365). A click hires (packet 0x36).
                const auto& offers=worldView.hirelings;
                const float rowHeight=44,w=420;
                const float h=40+rowHeight*float(std::max<size_t>(offers.size(),1))+34;
                const float x=width*.5f-w-20,y=std::max(20.f,height*.5f-h*.5f-40);
                panel(x,y,w,h);
                label(npcName,x+w*.5f,y+8,235,215,140,true);
                float ry=y+36;
                if(offers.empty())label(strings->byId(3365),x+w*.5f,ry+12,255,255,255,true);
                for(size_t i=0;i<offers.size();++i) {
                    const auto& o=offers[i];
                    const bool hot=mouseX>=x&&mouseX<=x+w&&mouseY>=ry&&mouseY<ry+rowHeight;
                    const auto value=[&](uint32_t id,int32_t n){return strings->byId(id)+": "+std::to_string(n);};
                    const bool dear=o.cost>worldView.vitals.gold+worldView.vitals.stashGold; // red: more than the player has
                    label(strings->byId(o.name),x+w*.5f,ry+2,hot?199:255,hot?179:255,hot?119:255,true);
                    label(value(3368,o.level)+"  "+value(3366,o.life)+"  "+value(3367,o.defense)+"  "+value(3369,o.cost),x+w*.5f,ry+18,
                          255,dear?80:255,dear?80:255,true);
                    if(o.description>0)if(const auto special=strings->byId(uint32_t(o.description));!special.empty())label(special,x+w*.5f,ry+32,160,160,160,true);
                    dialogRegions.push_back({x,ry,w,rowHeight,25,int(i),0,0});
                    ry+=rowHeight;
                }
                button(strings->byId(3400),x+w*.5f-60,y+h-30,120,4);
            } else if(npcDialog.page==NpcDialog::Page::Talk) {
                // What the NPC says scrolls up through the box while they speak (a click skips it).
                const auto shown=talkRows();
                const bool largerForTouch=touch::enabled();
                const float fitScale=std::max(1.f,float(safeHeight)-16.f)/227.f;
                const float scale=largerForTouch?std::max(.5f,std::min({std::max(1.f,hudScale),fitScale,
                    std::max(1.f,float(safeWidth)-16.f)/470.f})):1.f;
                const float rowHeight=19*scale,areaHeight=rowHeight*9;
                const float w=largerForTouch?std::min(470*scale,std::max(1.f,float(safeWidth)-16.f)):470.f;
                const float h=44*scale+areaHeight+12*scale;
                const float x=largerForTouch?safeLeft+(safeWidth-w)*.5f:width*.5f-w*.5f;
                const float y=largerForTouch?safeTop+(safeHeight-h)*.5f:20;
                const float areaTop=y+40*scale;
                panel(x,y,w,h);
                dialogRegions.push_back({x,y,w,h,20,5,0,0});
                const auto scaledLabel=[&](const std::string& caption,float centerX,float top,uint8_t r,uint8_t g,uint8_t b) {
                    const auto t=text(caption,r,g,b);
                    if(!t.w||!t.h)return;
                    const float textScale=std::min(scale,std::max(1.f,w-24*scale)/float(t.w));
                    const float textWidth=float(t.w)*textScale,textHeight=float(t.h)*textScale;
                    ui(centerX-textWidth*.5f+textScale,top+textScale,textWidth,textHeight,text(caption,0,0,0).image);
                    ui(centerX-textWidth*.5f,top,textWidth,textHeight,t.image);
                };
                if(largerForTouch)scaledLabel(npcName,x+w*.5f,y+10*scale,235,215,140);
                else label(npcName,x+w*.5f,y+10,235,215,140,true);
                const double elapsed=double(nowMilliseconds()-npcDialog.talkStart)/1000.0;
                const float progress=float(std::clamp(elapsed/std::max(0.1,npcDialog.talkSeconds),0.0,1.0));
                const float first=areaTop+areaHeight-progress*(rowHeight*float(shown.size())+areaHeight);
                for(size_t i=0;i<shown.size();++i) {
                    const float ty=first+rowHeight*float(i);
                    if(ty<areaTop||ty>areaTop+areaHeight-rowHeight||shown[i].empty())continue;
                    if(largerForTouch)scaledLabel(shown[i],x+w*.5f,ty,220,220,220);
                    else label(shown[i],x+w*.5f,ty,220,220,220,true);
                }
            } else if(uiAssets) {
                // Store: buysell at (80,60) with its tabs along the top, the 10 by 10 grid of inventory.txt "Monster2" (96,123), gold and the close button.
                std::vector<uint32_t> pages;
                for(const auto& item:worldView.items)if(item.vendor==uint32_t(npcDialog.guid)&&std::find(pages.begin(),pages.end(),item.page)==pages.end())pages.push_back(item.page);
                std::sort(pages.begin(),pages.end());
                if(!pages.empty()&&std::find(pages.begin(),pages.end(),npcDialog.tab)==pages.end())npcDialog.tab=pages.front();
                panelArt("panel\\buysell",0,false);
                dialogX=lx(80);dialogY=hy(60);dialogW=320*hudScale;dialogH=432*hudScale;
                dialogRegions.push_back({dialogX,dialogY,dialogW,dialogH,23,0,0,0});
                // buyselltabs: frames 0-3 the four tabs lifted (the page shown), 4-7 lowered. A store page is its storepage.txt row
                // (armor, weapons, magic, misc), which is also its tab's slot and its title (strings 4036-4039).
                for(const auto page:pages) {
                    if(page>3)continue;
                    const bool current=page==npcDialog.tab;const float tx=81.f+79.f*float(page);
                    put(uiAssets->frame("panel\\buyselltabs",page+(current?0:4)),false,tx,61);
                    if(strings)write(strings->byId(4036+page),current?white:gold,false,tx+39.5f,69,1);
                    dialogRegions.push_back({lx(tx),hy(61),79*hudScale,31*hudScale,21,int(page),0,0});
                }
                constexpr float cell=29,gridX=96,gridY=123;
                for(const auto& item:worldView.items) {
                    if(item.vendor!=uint32_t(npcDialog.guid)||item.page!=npcDialog.tab)continue;
                    const auto* info=itemTable->find(item.code);if(!info)continue;
                    const float ix=gridX+item.x*cell,iy=gridY+item.y*cell,iw=info->invWidth*cell,ih=info->invHeight*cell;
                    putItem(item,false,ix,iy,iw,ih);
                    dialogRegions.push_back({lx(ix),hy(iy),iw*hudScale,ih*hudScale,22,item.x,item.y,int32_t(item.id)});
                }
                write(std::to_string(worldView.vitals.gold),white,false,278,419,2);
                // The buttons in the boxes left of the close button (buysellbtn, normal and pressed in pairs): buy (2/3) and sell
                // (4/5), the one in use shown pressed, and for the smiths repair everything (18/19, pressed while held).
                // On a phone the icons are too small to read, so the mode is also a banner, the buttons are labeled, and
                // the one in use has a coloured frame.
                {
                    const bool smith=npcDialog.classId==154||npcDialog.classId==178||npcDialog.classId==253||npcDialog.classId==257||npcDialog.classId==511;
                    const bool pressing=leftButtonDown;
                    const bool buying=sellToNpc==0&&repairAtNpc==0;
                    const bool touchStore=touch::enabled();
                    if(touchStore&&!npcDialog.gamble) {
                        if(buying)modeCaption("BUYING",lx(90),hy(92),300*hudScale,26*hudScale,199,179,119,40,32,24);
                        else modeCaption("SELLING",lx(90),hy(92),300*hudScale,26*hudScale,255,90,70,90,28,22);
                    }
                    int tipAction=0; // the button under the mouse, which says what it does (as the original's button hints)
                    auto storeButton=[&](float x,uint32_t frame,bool selected,int action,const char* caption) {
                        const float size=32*hudScale;
                        const float target=touchStore?48*hudScale:32*hudScale;
                        const float sx=lx(x),sy=hy(443);
                        const float hitX=sx-(target-size)*.5f,hitY=sy-(target-size)*.5f;
                        const bool hovering=pressing&&mouseX>=hitX&&mouseX<=hitX+target&&mouseY>=hitY&&mouseY<=hitY+target;
                        const bool down=selected||hovering;
                        if(mouseX>=hitX&&mouseX<=hitX+target&&mouseY>=hitY&&mouseY<=hitY+target)tipAction=action;
                        if(touchStore) {
                            const bool sell=action==2;
                            const auto edge=selected?(sell?red:gold):(hovering?gold:grey);
                            const auto fill=selected?(sell?std::array<uint8_t,3>{110,32,24}:std::array<uint8_t,3>{90,74,40})
                                :(hovering?std::array<uint8_t,3>{70,60,40}:std::array<uint8_t,3>{28,24,20});
                            ui(hitX,hitY,target,target,solid(edge[0],edge[1],edge[2]));
                            ui(hitX+2,hitY+2,target-4,target-4,solid(fill[0],fill[1],fill[2]));
                        }
                        put(uiAssets->frame("panel\\buysellbtn",frame+(down?1:0)),false,x,443);
                        if(touchStore&&caption)write(caption,selected?(action==2?red:gold):white,false,x+16,474,1,2);
                        dialogRegions.push_back({hitX,hitY,target,target,24,action,0,0});
                    };
                    auto held=[&](float x){return pressing&&mouseX>=lx(x)&&mouseX<lx(x+32)&&mouseY>=hy(443)&&mouseY<hy(475);};
                    if(!npcDialog.gamble) {
                        storeButton(195,2,storeCursor==3,1,touchStore?"BUY":nullptr); // pressed only once chosen, as the original
                        storeButton(247,4,sellToNpc!=0,2,touchStore?"SELL":nullptr);
                    }
                    if(smith)storeButton(143,6,repairAtNpc!=0,5,touchStore?"FIX ONE":nullptr); // repair one item (6/7), on while chosen
                    if(smith)storeButton(299,18,held(299),3,touchStore?"REPAIR":nullptr);
                    if(!touchStore&&mouseX>=lx(351)&&mouseX<lx(383)&&mouseY>=hy(443)&&mouseY<hy(475))tipAction=4;
                    if(!touchStore&&tipAction&&text16) {
                        // Two lines over the button, the name in white and what a click does in gold, on a see-through box.
                        const float at[]={0,195,247,299,351,143};
                        const std::pair<const char*,const char*> tips[]={{"",""},
                            {"Buy","Click an item in the store to buy it"},
                            {"Sell","Click an item in your inventory to sell it"},
                            {"Repair All","Repair all your equipment"},
                            {"Close","Leave the store"},
                            {"Repair","Click an item to repair it"}};
                        const auto& [name,what]=tips[tipAction];
                        auto& small=text8?*text8:*text16;
                        const auto& top=text16->get(name,white,true);const auto& bottom=small.get(what,gold,true);
                        const float w=float(std::max(top.width,bottom.width))+8,h=float(top.height+bottom.height)+6;
                        const float cx=std::clamp(at[tipAction]+16,w*.5f+2,400-w*.5f-2),y=443-h-4;
                        shade(lx(cx-w*.5f),hy(y),w*hudScale,h*hudScale);
                        write(name,white,false,cx,y+2,1);
                        write(what,gold,false,cx,y+4+float(top.height),1,1);
                    }
                }
                closeButton(false,351,443,dialogRegions,20,4);
            } else {
                // Store: tabs for the pages the NPC has, and a 10 by 10 grid.
                std::vector<uint32_t> pages;
                for(const auto& item:worldView.items)if(item.vendor==uint32_t(npcDialog.guid)&&std::find(pages.begin(),pages.end(),item.page)==pages.end())pages.push_back(item.page);
                std::sort(pages.begin(),pages.end());
                if(!pages.empty()&&std::find(pages.begin(),pages.end(),npcDialog.tab)==pages.end())npcDialog.tab=pages.front();
                constexpr float cell=29;const float w=328,h=420;
                const float x=20,y=60;
                panel(x,y,w,h);
                label(npcName+(npcDialog.gamble?" - gamble":""),x+w*.5f,y+8,235,215,140,true);
                float tx=x+19;
                for(size_t i=0;i<pages.size();++i) {
                    const bool current=pages[i]==npcDialog.tab;
                    ui(tx,y+34,54,22,solid(90,74,44));ui(tx+1,y+35,52,20,current?solid(70,58,36):solid(30,26,20));
                    label("Tab "+std::to_string(i+1),tx+27,y+36,current?255:200,current?240:190,current?190:150,true);
                    dialogRegions.push_back({tx,y+34,54,22,21,int(pages[i]),0,0});
                    tx+=58;
                }
                const float gridX=x+19,gridY=y+66;
                for(int cy=0;cy<10;++cy)for(int cx=0;cx<10;++cx) {
                    ui(gridX+cx*cell,gridY+cy*cell,cell,cell,solid(60,54,44));ui(gridX+cx*cell+1,gridY+cy*cell+1,cell-2,cell-2,solid(28,25,22));
                }
                for(const auto& item:worldView.items) {
                    if(item.vendor!=uint32_t(npcDialog.guid)||item.page!=npcDialog.tab)continue;
                    const auto* info=itemTable->find(item.code);if(!info)continue;
                    const float ix=gridX+item.x*cell,iy=gridY+item.y*cell,iw=info->invWidth*cell,ih=info->invHeight*cell;
                    const auto c=backing(item);
                    const bool affordable=int32_t(item.price)<=worldView.vitals.gold;
                    ui(ix+1,iy+1,iw-2,ih-2,affordable?solid(c[0],c[1],c[2]):solid(90,20,20));
                    drawIcon(item,ix,iy,iw,ih,true);
                    dialogRegions.push_back({ix,iy,iw,ih,22,item.x,item.y,int32_t(item.id)});
                }
                label("Gold "+std::to_string(worldView.vitals.gold),x+20,y+h-56,235,215,140);
                button("Leave",x+w-140,y+h-42,120,4);
            }
        }
        if(worldView.tick<gameMessageUntil&&!gameMessage.empty()) {
            if(gameMessage.starts_with("Host:")) {
                const auto t=text(gameMessage,235,215,140);
                if(t.w&&t.h) {
                    const float maxTextWidth=std::max(1.f,safeWidth-32.f);
                    const float textScale=std::min(1.65f,maxTextWidth/float(t.w));
                    const float textWidth=float(t.w)*textScale,textHeight=float(t.h)*textScale;
                    const float padX=18*std::max(.5f,hudScale),padY=12*std::max(.5f,hudScale);
                    const float x=safeLeft+(safeWidth-textWidth-2*padX)*.5f;
                    const float y=safeTop+(safeHeight-textHeight-2*padY)*.5f;
                    const float border=2*std::max(.5f,hudScale);
                    ui(x,y,textWidth+2*padX,textHeight+2*padY,solid(146,121,73));
                    shade(x+border,y+border,textWidth+2*padX-2*border,textHeight+2*padY-2*border);
                    ui(x+padX+textScale,y+padY+textScale,textWidth,textHeight,text(gameMessage,0,0,0).image);
                    ui(x+padX,y+padY,textWidth,textHeight,t.image);
                }
            } else label(gameMessage,width*.5f,height*.5f,240,90,90,true);
        }
        // The waypoint menu: every discovered waypoint the game told us about (packet 0x63), click one to travel there (packet 0x49).
        waypointRegions.clear();
        if(waypointUp&&uiAssets) {
            // waygatebackground at (80,60), a tab per act (expwaygatetabs, 63x31: 2a lifted, 2a+1 lowered) and the act's waypoints in nine rows.
            static const int firstWaypoint[]={0,9,18,27,30,39};
            auto actOf=[](int level){return level<40?0:level<75?1:level<103?2:level<109?3:4;};
            if(waypointMenu.act<0)waypointMenu.act=actOf(int(worldView.level));
            const int act=std::clamp(waypointMenu.act,0,4);
            panelArt("menu\\waygatebackground",0,false);
            waypointPanelX=lx(80);waypointPanelY=hy(60);waypointPanelW=320*hudScale;waypointPanelH=432*hudScale;
            waypointRegions.push_back({waypointPanelX,waypointPanelY,waypointPanelW,waypointPanelH,36,0,0,0});
            for(int a=0;a<5;++a) {
                bool reached=false;for(int wp=firstWaypoint[a];wp<firstWaypoint[a+1];++wp)reached|=waypointMenu.activated(wp);
                if(!reached&&a!=act)continue;
                put(uiAssets->frame("menu\\expwaygatetabs",uint32_t(2*a+(a==act?0:1))),false,82.f+63.f*float(a),60);
                waypointRegions.push_back({lx(82.f+63.f*float(a)),hy(60),63*hudScale,31*hudScale,42,a,0,0});
            }
            for(int wp=firstWaypoint[act];wp<firstWaypoint[act+1];++wp) {
                const auto* level=levelTable->byWaypoint(wp);if(!level)continue;
                const float rowY=117.f+36.25f*float(wp-firstWaypoint[act]);
                const bool on=waypointMenu.activated(wp),here=level->id==int32_t(worldView.level);
                const bool hot=on&&mouseX>=lx(95)&&mouseX<=lx(385)&&mouseY>=hy(rowY)&&mouseY<=hy(rowY+33);
                // waygateicons: 0 a waypoint to travel to, 1 under the cursor, 2 the one in use, 3 not yet found.
                put(uiAssets->frame("menu\\waygateicons",!on?3u:here?2u:hot?1u:0u),false,97,rowY+2);
                write(level->name,on?(hot?white:gold):grey,false,140,rowY+9);
                if(on&&!here)waypointRegions.push_back({lx(95),hy(rowY),290*hudScale,33*hudScale,40,level->id,0,0});
            }
            closeButton(false,350,444,waypointRegions,41,0);
        } else if(waypointMenu.open&&levelTable&&!uiAssets) {
            std::vector<const d2::LevelInfo*> known;
            for(int wp=0;wp<112;++wp)if(waypointMenu.activated(wp))if(const auto* level=levelTable->byWaypoint(wp))known.push_back(level);
            const float rowHeight=30;const float w=280,h=70+rowHeight*float(std::max<size_t>(known.size(),1))+40;
            const float x=width*.5f-w*.5f,y=height*.5f-h*.5f;
            waypointPanelX=x;waypointPanelY=y;waypointPanelW=w;waypointPanelH=h;
            ui(x-3,y-3,w+6,h+6,solid(95,78,46));ui(x,y,w,h,solid(18,16,14));
            waypointRegions.push_back({x,y,w,h,36,0,0,0});
            label("Waypoint",x+w*.5f,y+10,235,215,140,true);
            float by=y+44;
            if(known.empty())label("No waypoints found yet",x+w*.5f,by,180,175,150,true);
            for(const auto* level:known) {
                const bool hot=mouseX>=x+20&&mouseX<=x+w-20&&mouseY>=by&&mouseY<=by+rowHeight-4;
                ui(x+20,by,w-40,rowHeight-4,solid(90,74,44));ui(x+21,by+1,w-42,rowHeight-6,hot?solid(70,58,36):solid(38,32,24));
                label(level->name,x+w*.5f,by+4,220,215,190,true);
                waypointRegions.push_back({x+20,by,w-40,rowHeight-4,40,level->id,0,0});
                by+=rowHeight;
            }
            const float closeY=y+h-36;
            const bool closeHot=mouseX>=x+w*.5f-60&&mouseX<=x+w*.5f+60&&mouseY>=closeY&&mouseY<=closeY+26;
            ui(x+w*.5f-60,closeY,120,26,solid(90,74,44));ui(x+w*.5f-59,closeY+1,118,24,closeHot?solid(70,58,36):solid(38,32,24));
            label("Close",x+w*.5f,closeY+3,235,215,140,true);
            waypointRegions.push_back({x+w*.5f-60,closeY,120,26,41,0,0,0});
        }
        // The hireling's panel, before the tooltip so the tooltip of what it wears lies over it.
        hirelingRegions.clear();
        if(hirelingUp) {
            // The hireling's panel (D2Client 0x6FAED600): panel\npcinv at (80,60); its name and life in the boxes under the
            // pictures, then the labels (table 0x6FB6F8D0) and values (0x6FB795F8) at their places in the panel, whose y are
            // D2Win's text baselines; what it wears in inventory.txt Hireling2's boxes; close (buysellbtn 10/11) at (352,445).
            panelArt("panel\\npcinv",0,false);
            const auto& h=worldView.hireling;
            const float px=80,py=60;
            hirelingRegions.push_back({lx(px),hy(py),320*hudScale,432*hudScale,82,0,0,0});
            auto label=[&](const std::string& raw,float x1,float x2,float y) {
                std::string t=raw;for(auto& c:t)if(c=='\r')c=' ';
                std::vector<std::string> rows;size_t at=0;
                while(at<=t.size()){size_t end=t.find('\n',at);if(end==std::string::npos)end=t.size();rows.push_back(t.substr(at,end-at));at=end+1;}
                // Two-line labels (the resistances) sit 4 pixels above and below the line, in font6.
                for(size_t i=0;i<rows.size();++i) {
                    const float row=rows.size()>1?y-4+8*float(i):y;
                    if(x2>0)write(rows[i],white,false,px+(x1+x2)*.5f,py+row-10,1,2);
                    else write(rows[i],white,false,px+x1,py+row-10,0,2);
                }
            };
            static const struct {int x1,x2,y;uint32_t text;} labels[]={{15,0,236,4058},{145,0,236,4057},{200,0,236,4059},{15,0,281,4060},
                {15,0,305,4062},{15,0,329,4061},{15,0,353,4064},{180,245,281,4071},{180,245,305,4072},{180,245,329,4073},{180,245,353,4074},{180,0,214,4068}};
            for(const auto& l:labels)label(strings->byId(l.text),float(l.x1),float(l.x2),float(l.y));
            // The values: right against x2 when there is no x1, centred between them otherwise, in font8 (font6 when wider than
            // the 48-pixel box); white, blue when items raise it, red when lowered.
            auto value=[&](const std::string& t,int x1,int x2,int y,std::array<uint8_t,3> color) {
                const int size=text8&&x1==0&&x2>0&&float(text8->font().width(t))>48?2:1;
                if(x1==0)write(t,color,false,px+float(x2),py+float(y)-12,2,size);
                else write(t,color,false,px+float(x1+x2)*.5f,py+float(y)-12,1,size);
            };
            auto against=[&](int32_t now,int32_t base){const std::array<uint8_t,3> raised{105,105,255};return now>base?raised:now<base?red:white;};
            const std::string name=mercName&&strings?strings->byId(mercName):std::string();
            write(name,white,false,px+15,py+214-12,0,1);
            value(std::to_string(h.experience),0,120,254,white);
            value(std::to_string(h.level),0,175,254,white);
            value(std::to_string(h.nextExperience),0,305,254,white);
            value(std::to_string(h.strength),0,154,282,against(h.strength,h.baseStrength));
            value(std::to_string(h.dexterity),0,154,306,against(h.dexterity,h.baseDexterity));
            value(std::to_string(h.minDamage)+"-"+std::to_string(h.maxDamage),0,154,330,white);
            value(std::to_string(h.defense),0,154,354,white);
            for(int i=0;i<4;++i)value(std::to_string(h.resist[size_t(i)]),0,309,282+24*i,h.resist[size_t(i)]<0?red:white);
            value(std::to_string(h.life)+" / "+std::to_string(h.maxLife),210,305,215,white);
            // What it wears, in its body boxes (inventory.txt Hireling2: head, torso, right arm, left arm).
            static const struct {int loc;float x1,x2,y1,y2;} slots[]={{1,215,269,68,119},{3,213,269,137,219},{4,100,155,107,219},{5,331,386,107,219}};
            for(const auto& s:slots) {
                const d2::ItemSnapshot* worn=nullptr;
                for(const auto& item:worldView.items)if(item.vendor==h.guid&&item.mode==1&&int(item.bodyLoc)==s.loc)worn=&item;
                if(worn)putItem(*worn,false,s.x1,s.y1,s.x2-s.x1,s.y2-s.y1,true);
                hirelingRegions.insert(hirelingRegions.begin(),{lx(s.x1),hy(s.y1),(s.x2-s.x1)*hudScale,(s.y2-s.y1)*hudScale,80,s.loc,0,worn?int32_t(worn->id):0});
            }
            closeButton(false,352,445,hirelingRegions,81,0);
            std::rotate(hirelingRegions.begin(),hirelingRegions.end()-1,hirelingRegions.end()); // close before the panel's catch-all
        }
        // Hover: the item under the cursor, with the same practical specs the inventory tooltip in Diablo II shows.
        {
            const d2::ItemSnapshot* hover=nullptr;
            auto find=[&](int32_t guid){for(const auto& item:worldView.items)if(int32_t(item.id)==guid)hover=&item;};
            for(const auto& region:dialogRegions)if(region.kind==22&&mouseX>=region.x&&mouseX<=region.x+region.w&&mouseY>=region.y&&mouseY<=region.y+region.h)find(region.item);
            if(!hover)for(const auto& region:inventoryRegions)if(region.item&&mouseX>=region.x&&mouseX<=region.x+region.w&&mouseY>=region.y&&mouseY<=region.y+region.h)find(region.item);
            if(!hover)for(const auto& region:storageRegions)if(region.item&&mouseX>=region.x&&mouseX<=region.x+region.w&&mouseY>=region.y&&mouseY<=region.y+region.h)find(region.item);
            if(!hover)for(const auto& region:beltRegions)if(region.item&&mouseX>=region.x&&mouseX<=region.x+region.w&&mouseY>=region.y&&mouseY<=region.y+region.h)find(region.item);
            if(!hover)for(const auto& region:hirelingRegions)if(region.kind==80&&region.item&&mouseX>=region.x&&mouseX<=region.x+region.w&&mouseY>=region.y&&mouseY<=region.y+region.h)find(region.item); // what the hireling wears
            if(hover&&itemTable&&!cursorItem) {
                const auto color=itemColor(*hover);
                const std::string name=shownName(*hover);
                struct TooltipLine {std::string s;std::array<uint8_t,3> c;bool centered=false;};
                std::vector<TooltipLine> lines{{name,color,true}};
                const auto* info=itemTable->find(hover->code);
                const bool identified=(hover->flags&0x10)!=0;
                auto add=[&](std::string s,std::array<uint8_t,3> c,bool centered=false){if(!s.empty())lines.push_back({std::move(s),c,centered});};
                auto damage=[&](int minValue,int maxValue,int fallbackMin,int fallbackMax,const char* prefix) {
                    if(minValue<=0&&maxValue<=0){minValue=fallbackMin;maxValue=fallbackMax;}
                    if(minValue>0||maxValue>0)add(std::string(prefix)+": "+std::to_string(minValue)+" to "+std::to_string(maxValue),white);
                };
                if(itemUnidentified(*hover))add("Unidentified",red,true);
                if(info) {
                    // Only the kinds of damage the item has (items.txt mindam, 2handmindam, minmisdam): a bonus to maximum damage
                    // raises all three of its stats, so a one-handed sword's other two are not zero.
                    if(info->minDamage>0||info->maxDamage>0)damage(identified?hover->minDamage:0,identified?hover->maxDamage:0,info->minDamage,info->maxDamage,"One-Hand Damage");
                    if(info->twoHandMinDamage>0||info->twoHandMaxDamage>0)damage(identified?hover->twoHandMinDamage:0,identified?hover->twoHandMaxDamage:0,info->twoHandMinDamage,info->twoHandMaxDamage,"Two-Hand Damage");
                    if(info->throwMinDamage>0||info->throwMaxDamage>0)damage(identified?hover->throwMinDamage:0,identified?hover->throwMaxDamage:0,info->throwMinDamage,info->throwMaxDamage,"Throw Damage");
                    // The defense with its magic (the full armorclass stat), blue when the magic changed it.
                    const int baseDefense=identified&&hover->armorClass>0?hover->armorClass:(info->maxArmor>0?info->maxArmor:info->minArmor);
                    const int defense=identified&&hover->armorTotal>0?hover->armorTotal:baseDefense;
                    if(defense>0)add("Defense: "+std::to_string(defense),defense!=baseDefense?std::array<uint8_t,3>{105,105,255}:white);
                    const int maxDurability=identified&&hover->maxDurability>0?hover->maxDurability:info->durability;
                    if(maxDurability>0)add("Durability: "+std::to_string(identified&&hover->durability>0?hover->durability:maxDurability)+" of "+std::to_string(maxDurability),white);
                    if(hover->quantity>0||info->maxStack>0)add("Quantity: "+std::to_string(hover->quantity>0?hover->quantity:info->maxStack)+(info->maxStack>0?" of "+std::to_string(info->maxStack):std::string()),white);
                    // The requirements by D2Common's rules (item_req_percent, ethereal, the item's own level), red when not met
                    // (ITEMS_CheckRequirements). The level shows above 1 only (D2Client 0x6FAE2F73).
                    const bool native=hover->reqStr||hover->reqDex||hover->reqLevel;
                    const int reqStr=native?hover->reqStr:info->reqStr,reqDex=native?hover->reqDex:info->reqDex,reqLevel=native?hover->reqLevel:info->reqLevel;
                    if(reqStr>0)add("Required Strength: "+std::to_string(reqStr),(native?(hover->reqMet&1):worldView.sheet.baseAttribute[0]>=reqStr)?white:red);
                    if(reqDex>0)add("Required Dexterity: "+std::to_string(reqDex),(native?(hover->reqMet&2):worldView.sheet.baseAttribute[2]>=reqDex)?white:red);
                    if(reqLevel>1)add("Required Level: "+std::to_string(reqLevel),(native?(hover->reqMet&4):worldView.vitals.level>=reqLevel)?white:red);
                }
                // The magic properties, blue, when the item is identified (D2Client 0x6FAF31A9: the item's STATLIST_MAGIC list).
                // What is in its sockets counts too: each filler keeps its own stat list (the gem's bonus for this kind of item)
                // linked to the item, so their values are added to the item's for the same stat and parameter.
                std::vector<d2::ItemSnapshot::Stat> magic=hover->stats;
                for(const auto& filler:worldView.items)if(filler.parent==hover->id&&filler.mode==6)
                    for(const auto& stat:filler.stats) {
                        auto same=std::find_if(magic.begin(),magic.end(),[&](const auto& m){return m.id==stat.id&&m.param==stat.param;});
                        if(same!=magic.end())same->value+=stat.value;else magic.push_back(stat);
                    }
                if(identified&&itemStats&&strings&&!magic.empty())
                    for(auto& line:d2::describeItemStats(magic,*itemStats,*strings,skillTable,worldView.vitals.level))add(std::move(line),{105,105,255},true);
                // The magic block's last line (D2Client 6FAE3EE0, coloured 3 = blue at 6FAE38AB): "Ethereal" (string 22745)
                // when the item is ethereal (flag 0x400000), ", " between, and "Socketed" (3453) + " " (3995) + "(n)" when it
                // has sockets (flag 0x800).
                if(strings&&(hover->flags&(0x400000|0x800))) {
                    std::string s=(hover->flags&0x400000)?strings->byId(22745):std::string();
                    if(hover->flags&0x800)s+=(s.empty()?"":", ")+strings->byId(3453)+strings->byId(3995)+"("+std::to_string(hover->sockets)+")";
                    add(s,{105,105,255},true);
                }
                // A set item's bonuses in force, green (D2Client 0x6FAF32B0 prints the item-set state lists the pieces worn switch on).
                if(identified&&itemStats&&strings&&!hover->setStats.empty())
                    for(auto& line:d2::describeItemStats(hover->setStats,*itemStats,*strings,skillTable,worldView.vitals.level))add(std::move(line),{24,255,0},true);
                // The set (6FAE2891-6FAE29F4, the tooltip text is built bottom up): when the item is worn, the set's own bonuses
                // on the player in gold (0x6FAF33C0: the item-set state list whose stat 71 is this set); then the set's name in
                // gold and its items, green those the player has (0x6FAE5990), red the others; a blank line before each part.
                if(identified&&hover->quality==5&&itemTable&&itemStats&&strings) {
                    const auto set=itemTable->setOf(hover->fileIndex);
                    const std::array<uint8_t,3> setGold{199,179,119};
                    if(set.set>=0) {
                        if(hover->mode==1)for(const auto& bonus:worldView.setBonuses)if(bonus.set==set.set&&!bonus.stats.empty()) {
                            lines.push_back({" ",white,true});
                            for(auto& line:d2::describeItemStats(bonus.stats,*itemStats,*strings,skillTable,worldView.vitals.level))add(std::move(line),setGold,true);
                            break;
                        }
                        lines.push_back({" ",white,true});
                        add(set.name,setGold,true);
                        for(const auto row:set.items) {
                            const bool owned=std::find(worldView.ownedSetItems.begin(),worldView.ownedSetItems.end(),uint16_t(row))!=worldView.ownedSetItems.end();
                            add(itemTable->displayName(hover->code,5,0,0,0,0,row),owned?std::array<uint8_t,3>{24,255,0}:std::array<uint8_t,3>{255,77,77},true);
                        }
                    }
                }
                const bool priced=hover->vendor&&hover->price;
                if(priced)add("Price: "+std::to_string(hover->price)+" gold",int32_t(hover->price)<=worldView.vitals.gold?std::array<uint8_t,3>{230,200,90}:std::array<uint8_t,3>{240,90,90},true);
                float tw=0,th=0;
                std::vector<HudText> rendered;rendered.reserve(lines.size());
                for(const auto& line:lines){rendered.push_back(text(line.s,line.c[0],line.c[1],line.c[2]));tw=std::max(tw,float(rendered.back().w));th+=float(rendered.back().h);}
                float x=std::min(float(mouseX)+16,float(width)-tw-14),y=std::min(float(mouseY)+16,float(height)-th-14);
                shade(x-8,y-4,tw+16,th+8);
                float ly=y;
                for(size_t i=0;i<lines.size();++i) {
                    const float lx=lines[i].centered?x+(tw-float(rendered[i].w))*.5f:x;
                    ui(lx,ly,float(rendered[i].w),float(rendered[i].h),rendered[i].image);
                    ly+=float(rendered[i].h);
                }
            }
        }
        // Skills: the tree window, the character window, the two skill boxes and the menu that changes them.
        if(skillTable&&skillArt&&worldView.vitals.maxLife>0) {
            auto skillIcon=[&](const d2::SkillInfo* info,float x,float y,float size,bool dim){
                const auto* image=info?skillArt->get(info->charClass,info->iconCel):nullptr;
                if(!image)return false;
                const auto& t=dim?dimIcon(*image):icon(*image);
                ui(x+(size-t.w)*.5f,y+(size-t.h)*.5f,float(t.w),float(t.h),t.image);
                return true;
            };
            auto skillLevel=[&](int id){for(const auto& s:worldView.skills)if(int(s.id)==id)return s.level;return 0;};
            auto skillCharges=[&](int id){for(const auto& s:worldView.skills)if(int(s.id)==id)return s.quantity;return 0;};
            // Scrolls, books and charged items keep a quantity with no skill level. The count sits at the icon's lower right in font6.
            auto drawCharges=[&](float x,float y,float size,int count){
                if(count<=0||!text6)return;
                const std::string digits=std::to_string(count);
                const auto& image=text6->get(digits,{255,255,255},true);
                const auto& shadow=text6->get(digits,{0,0,0},true);
                const float w=float(image.width)*hudScale,h=float(image.height)*hudScale;
                const float left=x+size-w-2.f*hudScale,top=y+size-h-1.f*hudScale;
                ui(left+hudScale,top+hudScale,w,h,&shadow);ui(left,top,w,h,&image);
            };
            auto skillRequirement=[&](const d2::SkillInfo* info)->std::string {
                if(!info||skillLevel(int(info->id))>0)return {};
                if(info->requiredLevel>0&&worldView.stats[12]<info->requiredLevel)return "Requires level "+std::to_string(info->requiredLevel);
                for(const auto req:info->prerequisites)if(req&&skillLevel(int(req))<=0) {
                    const auto* needed=skillTable->find(req);
                    return std::string("Requires ")+(needed?(needed->displayName.empty()?needed->name:needed->displayName):std::string("another skill"));
                }
                if(worldView.stats[5]<=0)return "No skill points";
                return {};
            };
            auto panel=[&](float x,float y,float w,float h,const std::string& title){
                ui(x-3,y-3,w+6,h+6,solid(95,78,46));ui(x,y,w,h,solid(18,16,14));
                label(title,x+w*.5f,y+8,235,215,140,true);
                skillRegions.push_back({x,y,w,h,36,0,0,0});
            };
            static const char* classNames[]={"Amazon","Sorceress","Necromancer","Paladin","Barbarian","Druid","Assassin"};
            const float bottomEdge=float(height);
            // The two skill boxes: the 48 pixel gaps of the control panel beside the globes (plain boxes beside the belt without the game's art).
            const float box=uiAssets?48*hudScale:48;
            const float boxY=uiAssets?hy(552):bottomEdge-box-14,leftBoxX=uiAssets?hx(117):width*.5f-2*32-box-14,rightBoxX=uiAssets?hx(635):width*.5f+2*32+14;
            for(int side=0;side<2;++side) {
                const float x=side?rightBoxX:leftBoxX;const int id=side?worldView.rightSkill:worldView.leftSkill;
                if(!uiAssets){ui(x-2,boxY-2,box+4,box+4,solid(95,78,46));ui(x,boxY,box,box,solid(20,18,15));}
                const auto* info=skillTable->find(uint32_t(id));
                const auto* image=info?skillArt->get(info->charClass,info->iconCel):nullptr;
                if(image&&uiAssets)ui(x,boxY,box,box,icon(*image).image);
                else if(!skillIcon(info,x,boxY,box,false))label(info&&!info->displayName.empty()?info->displayName:std::string(id==0?"Attack":"Skill"),x+box*.5f,boxY+box*.5f-9,220,215,190,true);
                drawCharges(x,boxY,box,skillCharges(id));
                skillRegions.push_back({x,boxY,box,box,side?33:32,0,0,0});
            }
            // Skills that can be chosen: everything the character has a level in, plus the basic attack.
            std::vector<int> choices{0};
            auto addChoice=[&](int id){if(std::find(choices.begin(),choices.end(),id)==choices.end())choices.push_back(id);};
            for(const auto& s:worldView.skills) {
                const auto* info=skillTable->find(s.id);
                if(info&&info->iconCel>=0&&(info->charClass==characterClass||info->charClass<0)&&(s.level>0||s.quantity>0))addChoice(int(s.id));
            }
            auto drawSkillMenu=[&] {
                if(!skillMenu)return;
                if(uiAssets) {
                    // As D2Client 0x6FB19720 lays the list out (800x600): rows 0-4 from the bottom, each skill of the player's list
                    // (in its order) in the row of its skilldesc.txt ListRow (0 general, 1-3 the class's pages), skills from items
                    // (charges, scrolls and books) in row 4; one per ListPool; the left list only skills.txt leftskill ones, no
                    // passives. The left list runs right from x 80, the right list left from x 672, 48 apart; the lowest row's
                    // icons stand from y 466 to 514 and each further row that has a skill 48 higher. The one in use outlined.
                    const bool left=skillMenu==1;
                    const float cell=48*hudScale;
                    std::vector<int> listed;
                    for(const auto& s:worldView.skills)listed.push_back(int(s.id));
                    if(std::find(listed.begin(),listed.end(),0)==listed.end())listed.insert(listed.begin(),0);
                    float rowTop=466.f;int lastRow=0;bool pools[256]{};
                    for(int row=0;row<5;++row) {
                        float x=left?80.f:672.f;
                        for(const int id:listed) {
                            const auto* info=skillTable->find(uint32_t(id));
                            if(!info||info->iconCel<0||info->passive||(left&&!info->leftSkill))continue;
                            const auto* known=[&]()->const d2::SkillSnapshot*{for(const auto& s:worldView.skills)if(int(s.id)==id)return &s;return nullptr;}();
                            const bool fromItem=skillCharges(id)>0||(known&&known->quantity>0);
                            const bool usable=id==0||fromItem||info->charClass<0||(known&&known->level>0);
                            if(!usable)continue;
                            if(row<4?(fromItem||info->listRow!=row):!fromItem)continue;
                            if(info->listPool>0&&info->listPool<256){if(pools[info->listPool])continue;pools[info->listPool]=true;}
                            if(lastRow<row){lastRow=row;rowTop-=48.f;}
                            const float cx=hx(x),cy=hy(rowTop);
                            const auto* image=skillArt->get(info->charClass,info->iconCel);
                            if(id==(left?worldView.leftSkill:worldView.rightSkill))ui(cx-2,cy-2,cell+4,cell+4,solid(199,179,119));
                            if(image)ui(cx,cy,cell,cell,icon(*image).image);
                            drawCharges(cx,cy,cell,skillCharges(id));
                            skillRegions.push_back({cx,cy,cell,cell,34,id,0,0});
                            x+=left?48.f:-48.f;
                        }
                    }
                } else {
                    constexpr float cell=52;constexpr int columns=6;
                    const int rows=(int(choices.size())+columns-1)/columns;
                    const float w=columns*cell+10,h=rows*cell+10;
                    const float x=std::clamp((skillMenu==1?leftBoxX:rightBoxX)+box*.5f-w*.5f,8.f,float(width)-w-8),y=boxY-h-10;
                    ui(x-3,y-3,w+6,h+6,solid(95,78,46));ui(x,y,w,h,solid(18,16,14));
                    skillRegions.push_back({x,y,w,h,36,0,0,0});
                    for(size_t i=0;i<choices.size();++i) {
                        const float cx=x+5+float(i%columns)*cell,cy=y+5+float(i/columns)*cell;const int id=choices[i];
                        const bool current=id==(skillMenu==1?worldView.leftSkill:worldView.rightSkill);
                        ui(cx,cy,cell-2,cell-2,current?solid(150,120,60):solid(60,54,44));ui(cx+1,cy+1,cell-4,cell-4,solid(24,21,18));
                        const auto* info=skillTable->find(uint32_t(id));
                        if(!skillIcon(info,cx,cy,cell-2,false))label(info&&!info->displayName.empty()?info->displayName:std::string("Attack"),cx+(cell-2)*.5f,cy+15,220,215,190,true);
                        drawCharges(cx,cy,cell-2,skillCharges(id));
                        skillRegions.push_back({cx,cy,cell-2,cell-2,34,id,0,0});
                    }
                }
            };
            if(treeUp&&uiAssets) {
                // skltree_X_back at (400,60): frames 0-3 the tab column, then the page (4-7 page 1, 8-11 page 2, 12-15 page 3) with its tab joined to it.
                static const char classLetter[]={'a','s','n','p','b','d','i'};
                const std::string file=std::string("spells\\skltree_")+(characterClass>=0&&characterClass<7?classLetter[characterClass]:'p')+"_back";
                skillTab=std::clamp(skillTab,1,3);
                panelArt(file,0,true);panelArt(file,uint32_t(4*skillTab),true);
                skillPanelX=rx(400);skillPanelY=hy(60);skillPanelW=320*hudScale;skillPanelH=432*hudScale;
                skillRegions.push_back({skillPanelX,skillPanelY,skillPanelW,skillPanelH,36,0,0,0});
                // Tab names from the string tables (page 3 is the top tab, page 1 the bottom one); Druid and Assassin names are not in 1.10's tables.
                static const std::vector<std::vector<int>> tabIds[]={
                    {{4236,4237},{4234,4235},{4232,4233}},{{4251,4231},{4250,4231},{4249,4231}},{{4245},{4243,4244,4231},{4242,4231}},
                    {{4241,4230},{4240,4239},{4238,4239}},{{4247,4230},{4247,4248},{4246}},
                    // Druid and Assassin: expansionstring.tbl StrSklTree26-34.
                    {{22509},{22510,22511},{22512}},{{22513},{22514,22515},{22516,22517}}};
                static const float tabTop[]={325,217,110},tabHeight[]={107,103,103};
                for(int page=1;page<=3;++page) {
                    std::string name;
                    if(characterClass>=0&&characterClass<7&&strings)for(int id:tabIds[characterClass][size_t(page-1)])name+=(name.empty()?"":"\n")+strings->byId(uint32_t(id));
                    const float top=60.f+tabTop[page-1],mid=top+tabHeight[page-1]*.5f;
                    const int lines=1+int(std::count(name.begin(),name.end(),'\n'));
                    write(name,page==skillTab?white:gold,true,676,mid-8.f*float(lines),1);
                    skillRegions.push_back({rx(632),hy(top),88*hudScale,tabHeight[page-1]*hudScale,31,page,0,0});
                }
                if(strings) {
                    write(strings->byId(4227)+" "+strings->byId(4228),white,true,676,70,1);
                    write(strings->byId(4229),white,true,676,86,1);
                }
                write(std::to_string(worldView.stats[5]),white,true,673,121,1);
                for(const auto* info:skillTable->tree(characterClass)) {
                    if(info->page!=skillTab)continue;
                    const float sx=415.f+69.f*float(info->column-1),sy=75.f+68.f*float(info->row-1);
                    const int level=skillLevel(int(info->id));
                    if(const auto* image=skillArt->get(info->charClass,info->iconCel))ui(rx(sx),hy(sy),48*hudScale,48*hudScale,(level>0?icon(*image):dimIcon(*image)).image);
                    if(level>0)write(std::to_string(level),white,true,sx+55,sy+41,1);
                    skillRegions.push_back({rx(sx),hy(sy),48*hudScale,48*hudScale,30,int(info->id),0,0});
                }
                // The close box is bottom left, or bottom centre on the middle page where a skill takes that corner.
                closeButton(true,skillTab==2?498.f:414.f,444,skillRegions,43,0);
            } else if(skillsOpen&&!uiAssets) {
                const float w=340,h=452,x=width-w-14,y=50;
                skillPanelX=x;skillPanelY=y;skillPanelW=w;skillPanelH=h;
                panel(x,y,w,h,std::string(characterClass>=0&&characterClass<7?classNames[characterClass]:"")+" skills");
                label("Skill points: "+std::to_string(worldView.stats[5]),x+w*.5f,y+30,worldView.stats[5]>0?255:200,worldView.stats[5]>0?240:200,worldView.stats[5]>0?150:200,true);
                for(int page=1;page<=3;++page) {
                    const float tx=x+20+(page-1)*100;const bool current=page==skillTab;
                    ui(tx,y+56,92,22,solid(90,74,44));ui(tx+1,y+57,90,20,current?solid(70,58,36):solid(30,26,20));
                    label("Page "+std::to_string(page),tx+46,y+58,current?255:200,current?240:190,current?190:150,true);
                    skillRegions.push_back({tx,y+56,92,22,31,page,0,0});
                }
                for(const auto* info:skillTable->tree(characterClass)) {
                    if(info->page!=skillTab)continue;
                    const float sx=x+34+(info->column-1)*100,sy=y+94+(info->row-1)*58;
                    const int level=skillLevel(int(info->id));
                    ui(sx-2,sy-2,54,54,level>0?solid(150,120,60):solid(60,54,44));ui(sx,sy,50,50,solid(24,21,18));
                    if(!skillIcon(info,sx,sy,50,level==0))label(info->displayName,sx+25,sy+16,180,175,150,true);
                    if(level>0)label(std::to_string(level),sx+34,sy+32,255,240,150);
                    skillRegions.push_back({sx,sy,50,50,30,int(info->id),0,0});
                }
                label("Click a skill to spend a point",x+w*.5f,y+h-28,150,140,120,true);
            }
            // Draw the picker after the skill tree/character panels so selected skills are visible above open panels.
            drawSkillMenu();
            questRegions.clear();
            if(questUp) {
                // The quest log (menu\questbackground at (80,60)): the act tabs (expquesttabs, a light and a dark frame per act)
                // along the top, the act's quests in sockets (questsockets; 1 the chosen one) in two rows of three, and the
                // chosen quest's name and state in the box below. A quest shows the skull seal (questbutton) until the game
                // reveals it, its own picture (the last frame of aXqY) while it is under way, and its stone picture (questdone)
                // once done.
                panelArt("menu\\questbackground",0,false);
                questRegions.push_back({lx(80),hy(60),320*hudScale,432*hudScale,73,0,0,0});
                static const int firstQuest[5]={1,9,17,25,35},firstPicture[5]={0,6,12,18,21};
                const int act=std::clamp(questLog.act,0,4),count=act==3?3:6;
                for(int a=0;a<5;++a) {
                    const float tx=83.f+63.f*float(a),ty=60;
                    put(uiAssets->frame("menu\\expquesttabs",uint32_t(a*2+(a==act?0:1))),false,tx,ty);
                    questRegions.push_back({lx(tx),hy(ty),63*hudScale,31*hudScale,71,a,0,0});
                }
                const int selected=std::clamp(questLog.selected,0,count-1);
                for(int q=0;q<count;++q) {
                    const float sx=100.f+100.f*float(q%3),sy=94.f+96.f*float(q/3);
                    const int number=firstQuest[act]+q;const uint16_t flags=questLog.flags[size_t(number)];
                    const bool done=(flags&1)||(flags&0x8000),started=done||(flags&0x6)||questLog.state[size_t(number)];
                    put(uiAssets->frame("menu\\questsockets",q==selected?1:0),false,sx,sy);
                    // Frame 0 of aXqY is the quest under way (gold), its last frame the quest done (burnt stone); in between it burns,
                    // which plays once when the log first shows a quest completed in this game (flag bit 14).
                    uint32_t frame=done?26:0;
                    if(done&&(flags&0x4000)) {
                        auto& start=questLog.burnStart[size_t(number)];
                        if(!start)start=worldView.tick+1;
                        frame=uint32_t(std::min<uint64_t>(26,(worldView.tick+1-start)/2));
                    }
                    const d2::Image* picture=started?uiAssets->frame("menu\\a"+std::to_string(act+1)+"q"+std::to_string(q+1),frame)
                        :uiAssets->frame("menu\\questdone",uint32_t(firstPicture[act]+q)); // not given yet: grey
                    put(picture,false,sx+4,sy+5);
                    questRegions.push_back({lx(sx),hy(sy),80*hudScale,95*hudScale,70,q,0,0});
                }
                {
                    // The chosen quest: its name, then the line for its state (qstsaXqYN), wrapped to the box.
                    const int number=firstQuest[act]+selected;const uint16_t flags=questLog.flags[size_t(number)];
                    const std::string key="qstsa"+std::to_string(act+1)+"q"+std::to_string(selected+1);
                    const bool done=(flags&1)||(flags&0x8000),started=done||(flags&0x6)||questLog.state[size_t(number)];
                    if(started)write(strings->get(key),gold,false,240,322,1);
                    std::string words;
                    if(flags&1)words=strings->byId(3726);                    // Quest completed.
                    else if(flags&0x8000)words=strings->byId(3728);          // completed in a previous game
                    else if(const int state=questLog.state[size_t(number)]) {
                        words=strings->get(key+std::to_string(state));
                        if(words==key+std::to_string(state))words.clear();
                        if(number==1&&state==4)words+=std::to_string(questLog.monstersLeft); // "Monsters remaining: "
                    }
                    for(auto& c:words)if(c=='\n'||c=='\r')c=' ';
                    std::vector<std::string> lines;std::string line,word;
                    for(size_t i=0;i<=words.size();++i) {
                        if(i<words.size()&&words[i]!=' '){word+=words[i];continue;}
                        if(word.empty())continue;
                        const std::string joined=line.empty()?word:line+" "+word;
                        if(!text16||text16->font().width(joined)<=280)line=joined;else{lines.push_back(line);line=word;}
                        word.clear();
                    }
                    if(!line.empty())lines.push_back(line);
                    for(size_t i=0;i<lines.size()&&i<6;++i)write(lines[i],white,false,240,345.f+16.f*float(i),1);
                }
                closeButton(false,359,452,questRegions,72,0);
            }
            if(characterUp&&uiAssets) {
                // invchar6 frames 0-3 at (80,60): name and class, level and experience, the four attributes with what they give, and resistances.
                panelArt(expansionGame?"panel\\invchar6":"panel\\invchar",0,false);
                characterPanelX=lx(80);characterPanelY=hy(60);characterPanelW=320*hudScale;characterPanelH=432*hudScale;
                const auto& st=worldView.stats;
                // Labels on one line: the tables split some with '|' or a line break and leave a "%s" for what the game fills in.
                auto s=[&](int id){
                    std::string t=strings?strings->byId(uint32_t(id)):std::string();
                    if(const auto p=t.find("%s");p!=std::string::npos)t.erase(p,2);
                    for(auto& c:t)if(c=='|'||c=='\n'||c=='\r')c=' ';
                    while(!t.empty()&&t.front()==' ')t.erase(t.begin());
                    return t;
                };
                // A label the table splits in two ('|', "Lightning|Resistance"): both lines in the largest font that fits the box's
                // width and its height (the two drawn lines within the box's 16 pixels).
                auto fitLines=[&](const std::string& first,const std::string& second,float x,float y,float boxWidth,std::array<uint8_t,3> color) {
                    int size=2;float lineHeight=8;
                    for(int candidate:{0,1,2}) {
                        d2::TextCache* cache=candidate==0?text16:candidate==1?text8:text6;if(!cache)continue;
                        const auto& a=cache->get(first,color,true);const auto& b=cache->get(second,color,true);
                        const float widest=float(std::max(a.width,b.width)),tall=float(std::max(a.height,b.height));
                        size=candidate;lineHeight=tall;
                        if(std::getenv("D2MODERN_LABEL_LOG"))std::cout<<"Label font "<<candidate<<": "<<widest<<" by "<<tall<<" for "<<first<<std::endl;
                        if(widest<=boxWidth-4&&tall*2<=16)break;
                    }
                    // The box's inside runs from y to y+17: one line under the other from its top.
                    // The line pictures carry the font's blank rows: font6's letters stand 8 apart.
                    const float gap=size==2?8.f:lineHeight-1;
                    write(first,color,false,x,y-1,1,size);write(second,color,false,x,y-1+gap,1,size);
                };
                auto fit=[&](const std::string& t,float x,float y,float boxWidth,std::array<uint8_t,3> color) {
                    // The largest of the game's fonts that fits the box.
                    int size=0;
                    if(text16&&float(text16->font().width(t))>boxWidth-4)size=text8&&float(text8->font().width(t))<=boxWidth-4?1:2;
                    write(t,color,false,x,y+float(size)*2,1,size);
                };
                auto fitCentered=[&](const std::string& t,float x,float y,float boxWidth,std::array<uint8_t,3> color) {
                    int size=0;
                    if(text16&&float(text16->font().width(t))>boxWidth-4)size=text8&&float(text8->font().width(t))<=boxWidth-4?1:2;
                    write(t,color,false,x,y+float(size)*2,1,size);
                };
                const auto& sheet=worldView.sheet;
                const std::array<uint8_t,3> raised{105,105,255}; // a value items raised, in the magic items' blue
                // Damage, attack rating, defense and resistances are coloured by the player's states, not by items (D2Client
                // 0x6FAD0B99-0x6FAD0D3A): blue while a state has the value's states.txt "blue" column (an aura, a buff), red
                // with its "red" column (a curse); a resistance at its cap shows the cap in gold (colour 4) unless blue.
                const auto [stateBlue,stateRed]=gameStates?gameStates->sheetColors(worldView.player().states):std::pair<uint8_t,uint8_t>{0,0};
                auto byState=[&](int bit){return (stateRed&(1u<<bit))?red:(stateBlue&(1u<<bit))?raised:white;};
                const std::array<uint8_t,3> capGold{199,179,119};
                // Defense is also blue with a shield on and Holy Shield (state 101) up (0x6FAD0D01).
                const bool shieldOn=[&]{for(const auto& item:worldView.items)if(item.mode==1&&!item.vendor&&(item.bodyLoc==4||item.bodyLoc==5))if(const auto* info=itemTable?itemTable->find(item.code):nullptr;info&&(info->type=="shie"||info->type=="ashd"||info->type=="pala"||info->type=="head"))return true;return false;}();
                const bool holyShield=(worldView.player().states[101>>5]&(1u<<(101&31)))!=0;
                const auto defenseColor=(stateRed&4)?red:((stateBlue&4)||(shieldOn&&holyShield))?raised:white;
                static const int classIds[]={4011,4010,4009,4008,4007,4012,4013};
                write(characterName,white,false,176,70,1);
                if(characterClass>=0&&characterClass<7)write(s(classIds[characterClass]),white,false,331,70,1);
                fit(s(4057),112,93,43,white);write(std::to_string(st[12]),white,false,112,110,1);
                fit(s(4058),203,93,119,white);write(std::to_string(uint32_t(st[13])),white,false,203,110,1);
                fit(s(4059),331,93,119,white);if(st[12]<99)write(std::to_string(uint32_t(st[30])),white,false,331,110,1); // none past the last level
                struct Attribute {int label,stat;float y;};
                static const Attribute attributes[]={{4060,0,143},{4062,2,206},{4066,3,292},{4069,1,354}};
                for(const auto& a:attributes) {
                    const int32_t base=sheet.baseAttribute[size_t(a.stat)];
                    fit(s(a.label),123,a.y,66,white);write(std::to_string(st[a.stat]),st[a.stat]>base?raised:st[a.stat]<base?red:white,false,177,a.y,1);
                    if(st[4]>0){put(uiAssets->frame("panel\\level",0),false,203,a.y-6);skillRegions.push_back({lx(203),hy(a.y-6),30*hudScale,30*hudScale,35,a.stat,0,0});}
                }
                // Two rows each for damage and attack rating: the left skill's, then the right skill's (labelled with its name);
                // a skill that doesn't strike with the weapon leaves its row empty.
                const std::string rightName=[&]{const auto* info=skillTable->find(uint32_t(std::max(0,worldView.rightSkill)));return info?info->displayName:std::string();}();
                auto damageText=[&](int side){return sheet.skillDamageMax[size_t(side)]?std::to_string(sheet.skillDamageMin[size_t(side)])+"-"+std::to_string(sheet.skillDamageMax[size_t(side)]):std::string();};
                auto attackText=[&](int side){return sheet.skillAttack[size_t(side)]?std::to_string(sheet.skillAttack[size_t(side)]):std::string();};
                fit(s(4061),290,143,99,white);fitCentered(damageText(0),365,143,50,byState(1));
                if(!damageText(1).empty()){fit(rightName,290,166,99,white);fitCentered(damageText(1),365,166,50,byState(1));}
                fit(s(4063),295,206,109,white);fitCentered(attackText(0),371,206,40,byState(0));
                if(!attackText(1).empty()){fit(rightName,295,229,109,white);fitCentered(attackText(1),371,229,40,byState(0));}
                fit(s(4064),295,254,109,white);fitCentered(std::to_string(sheet.defense),371,254,40,defenseColor);
                // The maxima against their value without items (D2Client 0x6FAD0A3A, fields 7, 9, 11): blue above, red below.
                auto maxColor=[&](int stat,int which){const int32_t base=sheet.baseMax[size_t(which)];return base<=0?white:st[stat]>base?raised:st[stat]<base?red:white;};
                fit(s(4067),276,292,70,white);write(std::to_string(st[10]>>8),white,false,331,292,1);write(std::to_string(st[11]>>8),maxColor(11,2),false,371,292,1);
                fit(s(4068),276,316,70,white);write(std::to_string(st[6]>>8),white,false,331,316,1);write(std::to_string(st[7]>>8),maxColor(7,0),false,371,316,1);
                fit(s(4070),276,354,70,white);write(std::to_string(st[8]>>8),white,false,331,354,1);write(std::to_string(st[9]>>8),maxColor(9,1),false,371,354,1);
                for(int i=0;i<4;++i) {
                    // Fire, cold, lightning, poison (states.txt rf, rc, rl, rp): below zero red, at the cap gold.
                    static const int bits[4]={3,5,4,6};
                    const float y=392.f+24.f*float(i);
                    {   // The table's two lines ("Fire|Resistance").
                        std::string label=strings?strings->byId(uint32_t(4071+i)):std::string();
                        if(const auto bar=label.find_first_of("|\n");bar!=std::string::npos)fitLines(label.substr(0,bar),label.substr(bar+1),302,y,97,white);
                        else fit(s(4071+i),302,y,97,white);
                    }
                    const int32_t value=sheet.resist[size_t(i)];
                    auto color=byState(bits[i]);
                    if(value>=sheet.resistMax[size_t(i)]&&color!=raised)color=capGold;
                    if(value<0)color=red;
                    fitCentered(std::to_string(value)+"%",371,y,40,color);
                }
                if(st[4]>0) {
                    write(s(4075),white,false,146,450,1,1);write(s(4076),white,false,146,462,1,1);
                    write(std::to_string(st[4]),white,false,224,456,1);
                }
                // The close button in the empty box at the bottom of the art (its gold frame 207-240 by 449-482).
                closeButton(false,208,449,skillRegions,48,0);
                skillRegions.push_back({characterPanelX,characterPanelY,characterPanelW,characterPanelH,36,0,0,0});
            } else if(characterOpen&&!uiAssets&&!(npcDialog.open&&npcDialog.page==NpcDialog::Page::Shop)) {
                const float w=328,h=452,x=20,y=50;
                characterPanelX=x;characterPanelY=y;characterPanelW=w;characterPanelH=h;
                panel(x,y,w,h,"Character");
                const auto& st=worldView.stats;
                label(std::string(characterClass>=0&&characterClass<7?classNames[characterClass]:"Hero")+", level "+std::to_string(st[12]),x+w*.5f,y+32,220,215,190,true);
                label("Experience  "+std::to_string(uint32_t(st[13]))+" / "+std::to_string(uint32_t(st[30])),x+w*.5f,y+56,200,200,200,true);
                struct Attribute {const char* name;int stat;};
                static const Attribute attributes[]={{"Strength",0},{"Dexterity",2},{"Vitality",3},{"Energy",1}};
                float ay=y+100;
                for(const auto& attribute:attributes) {
                    label(attribute.name,x+30,ay,235,215,140);
                    label(std::to_string(st[attribute.stat]),x+w-110,ay,255,255,255);
                    if(st[4]>0) {
                        const bool hot=mouseX>=x+w-70&&mouseX<=x+w-46&&mouseY>=ay-2&&mouseY<=ay+22;
                        ui(x+w-70,ay-2,24,24,solid(90,74,44));ui(x+w-69,ay-1,22,22,hot?solid(70,58,36):solid(38,32,24));
                        label("+",x+w-58,ay,235,215,140,true);
                        skillRegions.push_back({x+w-70,ay-2,24,24,35,attribute.stat,0,0});
                    }
                    ay+=36;
                }
                label("Stat points left: "+std::to_string(st[4]),x+30,ay+4,st[4]>0?255:200,st[4]>0?240:200,st[4]>0?150:200);
                ay+=50;
                label("Life  "+std::to_string(st[6]>>8)+" / "+std::to_string(st[7]>>8),x+30,ay,220,80,80);ay+=28;
                label("Mana  "+std::to_string(st[8]>>8)+" / "+std::to_string(st[9]>>8),x+30,ay,110,130,240);ay+=28;
                label("Stamina  "+std::to_string(st[10]>>8)+" / "+std::to_string(st[11]>>8),x+30,ay,220,215,150);ay+=28;
                label("Defense  "+std::to_string(st[31]),x+30,ay,200,200,200);
            }
            if(!uiAssets&&(worldView.stats[4]>0||worldView.stats[5]>0)) {
                std::string hint="Level up:";
                if(worldView.stats[4]>0)hint+="  "+std::to_string(worldView.stats[4])+" stat points (C)";
                if(worldView.stats[5]>0)hint+="  "+std::to_string(worldView.stats[5])+" skill points (T)";
                label(hint,16,bottomEdge-84,255,240,150);
            }
            // Hover: the skill under the cursor.
            for(const auto& region:skillRegions) {
                if((region.kind!=30&&region.kind!=34&&region.kind!=32&&region.kind!=33)||mouseX<region.x||mouseX>region.x+region.w||mouseY<region.y||mouseY>region.y+region.h)continue;
                const int id=region.kind==32?worldView.leftSkill:region.kind==33?worldView.rightSkill:region.a;
                const auto* info=skillTable->find(uint32_t(id));if(!info)break;
                std::string name=info->displayName.empty()?info->name:info->displayName;
                if(skillLevel(id)>0&&region.kind!=34)name+="  (level "+std::to_string(skillLevel(id))+")";
                if(const int charges=skillCharges(id);charges>0)name+="  ("+std::to_string(charges)+")";
                if(region.kind==30)if(const auto req=skillRequirement(info);!req.empty())name+="\n"+req;
                const auto& t=text(name,235,215,140);
                const float x=std::min(float(mouseX)+16,float(width)-t.w-14),y=std::max(6.f,float(mouseY)-t.h-16);
                shade(x-4,y-2,float(t.w)+8,float(t.h)+4);ui(x,y,float(t.w),float(t.h),t.image);
                break;
            }
        }
        beltRegions.clear();
        if(itemTable&&itemArt&&worldView.vitals.maxLife>0&&uiAssets) {
            // The four belt slots of the control panel, 31 pixels apart.
            for(int column=0;column<4;++column) {
                const float x=hx(422.f+31*column),y=hy(561),w=31*hudScale,h=32*hudScale;
                const int32_t guid=beltItem(column);
                for(const auto& item:worldView.items)if(int32_t(item.id)==guid)if(const auto* image=itemArt->get(item.code,shownQuality(item),item.invGraphic,item.invTrans,item.invColor)) {
                    const float iw=image->width*hudScale,ih=image->height*hudScale;
                    ui(x+(w-iw)*.5f,y+(h-ih)*.5f,iw,ih,icon(*image).image);
                }
                beltRegions.push_back({x,y,w,h,3,column,column,guid}); // b: the belt slot (0..15, row*4+column)
            }
            // The belt's other rows (as many as the worn belt has) pop up above it, one ctrlpnl_popbelt strip per row.
            uint8_t rows=1;
            for(const auto& item:worldView.items)if(item.mode==1&&!item.vendor&&item.bodyLoc==8)if(const auto* info=itemTable->find(item.code))rows=std::max<uint8_t>(1,info->beltRows);
            if(beltOpen)for(int row=1;row<rows;++row) {
                const float stripY=561.f-32.f*row;
                sprite(uiAssets->frame("panel\\ctrlpnl_popbelt",0),420,stripY);
                for(int column=0;column<4;++column) {
                    const float x=hx(422.f+31*column),y=hy(stripY),w=31*hudScale,h=32*hudScale;
                    int32_t guid=0;
                    // A belt item's x is its slot in D2Common's 16 by 1 belt grid (y is 0): row*4+column.
                    for(const auto& item:worldView.items)if(item.mode==2&&item.x==row*4+column) {
                        guid=int32_t(item.id);
                        if(const auto* image=itemArt->get(item.code,shownQuality(item),item.invGraphic,item.invTrans,item.invColor)){const float iw=image->width*hudScale,ih=image->height*hudScale;ui(x+(w-iw)*.5f,y+(h-ih)*.5f,iw,ih,icon(*image).image);}
                    }
                    beltRegions.push_back({x,y,w,h,3,column,row*4+column,guid});
                }
            }
        } else if(itemTable&&itemArt&&worldView.vitals.maxLife>0) {
            constexpr float slot=32;const float bx=width*.5f-2*slot,by=float(height)-slot-14;
            for(int column=0;column<4;++column) {
                const float x=bx+column*slot;
                ui(x,by,slot,slot,solid(70,62,50));ui(x+1,by+1,slot-2,slot-2,solid(28,25,22));
                const int32_t guid=beltItem(column);
                for(const auto& item:worldView.items)if(int32_t(item.id)==guid)drawIcon(item,x,by,slot,slot,true);
                label(std::to_string(column+1),x+3,by,200,200,200);
                beltRegions.push_back({x,by,slot,slot,3,column,column,guid});
            }
        }
        for(const auto& item:worldView.items)if(item.mode==4)drawIcon(item,float(mouseX)-cursorItemW*14.5f,float(mouseY)-cursorItemH*14.5f,cursorItemW*29.f,cursorItemH*29.f,true);
        const auto& v=worldView.vitals;
        const float bottom=float(height);
        if(uiAssets) {
            // Hovering a globe names its value above it, as the game does.
            auto overGlobe=[&](float x){return mouseX>=hx(x)&&mouseX<=hx(x+80)&&mouseY>=hy(511)&&mouseY<=hy(591);};
            if(overGlobe(30))label("Life: "+std::to_string(v.life)+" / "+std::to_string(v.maxLife),hx(70),hy(488),255,255,255,true);
            if(overGlobe(690))label("Mana: "+std::to_string(v.mana)+" / "+std::to_string(v.maxMana),hx(730),hy(488),255,255,255,true);
        } else {
            bar(20,bottom-56,220,16,v.maxLife?float(v.life)/v.maxLife:0,{190,25,25});
            label("Life "+std::to_string(v.life)+" / "+std::to_string(v.maxLife),26,bottom-58,255,255,255);
            bar(20,bottom-30,220,16,v.maxMana?float(v.mana)/v.maxMana:0,{40,80,215});
            label("Mana "+std::to_string(v.mana)+" / "+std::to_string(v.maxMana),26,bottom-32,255,255,255);
        }
        static const std::map<uint32_t,const char*> areas={{1,"Rogue Encampment"},{2,"Blood Moor"},{3,"Cold Plains"},{4,"Stony Field"}};
        const auto area=areas.find(worldView.level);
        // Level entrances: name them under the cursor (and outline the clickable rectangle with the collision overlay, F3).
        if(scene&&walkGrid)for(const auto& box:objectPicks) {
            if(!(box.guid&d2::tileIdBit))continue;
            const float sx=width*.5f+(box.x-scene->width*.5f)*zoom+panX,sy=height*.5f+(box.y-scene->height*.5f)*zoom+panY,sw=box.w*zoom,sh=box.h*zoom;
            if(showCollision){bar(sx,sy,sw,2,1,{255,200,60});bar(sx,sy+sh-2,sw,2,1,{255,200,60});bar(sx,sy,2,sh,1,{255,200,60});bar(sx+sw-2,sy,2,sh,1,{255,200,60});}
            if(mouseX>=sx&&mouseX<=sx+sw&&mouseY>=sy&&mouseY<=sy+sh) {
                // Where it leads, as the game names it ("To The Den of Evil"): this level's entrance with the tile's warp id.
                std::string name="Entrance";
                if(levelTable)if(const auto* here=levelTable->byId(int32_t(worldView.level)))
                    for(const auto& tile:worldView.tiles)if(tile.id==box.guid)
                        for(int i=0;i<8;++i)if(here->warp[i]==int(tile.classId)&&here->vis[i]>0)if(const auto* there=levelTable->byId(here->vis[i]);there&&!there->warpText.empty()) {
                            name=strings?strings->get(there->warpText):there->warpText;if(name.empty())name=there->warpText;
                        }
                const auto& t=text(name,255,255,255);
                const float x=sx+sw*.5f-t.w*.5f,y=sy-t.h-4;
                shade(x-4,y-2,float(t.w)+8,float(t.h)+4);ui(x,y,float(t.w),float(t.h),t.image);
            }
        }
        // The object under the cursor: its name (objects.txt Name, in the game's language) over it.
        if(scene&&walkGrid&&hoveredObject&&objectTable)for(const auto& box:objectPicks) {
            if(box.guid!=hoveredObject)continue;
            const d2::ObjectInfo* info=nullptr;
            for(const auto& o:worldView.objects)if(o.id==hoveredObject)info=objectTable->find(o.classId);
            if(!info||info->name.empty())break;
            std::string name=strings?strings->get(info->name):info->name;
            if(!name.empty())name[0]=char(std::toupper(static_cast<unsigned char>(name[0])));
            const float sx=width*.5f+(box.x-scene->width*.5f)*zoom+panX,sy=height*.5f+(box.y-scene->height*.5f)*zoom+panY,sw=box.w*zoom;
            const auto& t=text(name,255,255,255);
            const float x=sx+sw*.5f-t.w*.5f,y=sy-t.h-4;
            shade(x-4,y-2,float(t.w)+8,float(t.h)+4);ui(x,y,float(t.w),float(t.h),t.image);
            break;
        }
        // Town people and enemies under the cursor: the same over-the-sprite label objects use.
        if(scene&&walkGrid&&hoveredUnit&&unitArt)for(const auto& box:unitPicks) {
            if(box.guid!=hoveredUnit)continue;
            const d2::EntitySnapshot* unit=nullptr;
            for(const auto& e:worldView.entities)if(e.id>=1000&&uint32_t(e.id-1000)==hoveredUnit)unit=&e;
            const uint32_t classId=unit?unit->classId:box.classId;
            const uint8_t kind=unit?uint8_t(unit->kind):box.kind;
            const uint8_t monsterType=unit?unit->monsterType:box.monsterType;
            const auto* sprite=unitArt->sprite(classId);
            std::string name=sprite?(strings?strings->get(sprite->nameKey):sprite->id):(kind==1?"Person":"Monster");
            if(unit&&strings&&kind==2&&(unit->monsterType&2)&&unit->name[0])name=strings->byId(unit->name[0]);
            else if(unit&&strings&&kind==2&&(unit->monsterType&8)&&!(unit->monsterType&4)&&unit->name[0])name=strings->byId(unit->name[0])+strings->byId(unit->name[1])+" "+strings->byId(unit->name[2]);
            if(!name.empty())name[0]=char(std::toupper(static_cast<unsigned char>(name[0])));
            const bool champion=kind==2&&(monsterType&4)&&!(monsterType&2),unique=kind==2&&!champion&&(monsterType&(2|8));
            const uint8_t r=unique?199:champion?105:255,g=unique?179:champion?105:255,b=unique?119:champion?255:255;
            const float sx=width*.5f+(box.x-scene->width*.5f)*zoom+panX,sy=height*.5f+(box.y-scene->height*.5f)*zoom+panY,sw=box.w*zoom;
            const auto& t=text(name,r,g,b);
            const float x=sx+sw*.5f-t.w*.5f,y=sy-t.h-4;
            shade(x-4,y-2,float(t.w)+8,float(t.h)+4);ui(x,y,float(t.w),float(t.h),t.image);
            break;
        }
        // A corpse under the cursor: its name (D2Game names the body after the player), as over a town person.
        if(scene&&walkGrid&&!characterName.empty()) {
            const float mx=(mouseX-width*.5f-panX)/zoom+scene->width*.5f,my=(mouseY-height*.5f-panY)/zoom+scene->height*.5f;
            if(const auto* body=pickCorpse(mx,my)) {
                const auto at=d2::projectPoint(*scene,float(body->position.x)/d2::fixedOne,float(body->position.y)/d2::fixedOne);
                const float sx=width*.5f+(at[0]-scene->width*.5f)*zoom+panX,sy=height*.5f+(at[1]-scene->height*.5f)*zoom+panY;
                const auto& t=text(characterName,255,255,255);
                const float x=sx-t.w*.5f,y=sy-40.f*zoom-t.h;
                shade(x-4,y-2,float(t.w)+8,float(t.h)+4);ui(x,y,float(t.w),float(t.h),t.image);
            }
        }
        if(goldPopup.open&&uiAssets) {
            // The gold popup (bigmenu\popupok, 264 by 176, in the middle of the screen): the game's question (strings 4033 drop,
            // 4049 deposit, 4050 withdraw), the amount in the dark field, accept and cancel (menu\goldbtn 0/1 and 2/3) beside it.
            constexpr float ox=400-132,oy=300-88;
            auto at=[&](const d2::Image* image,float x,float y){if(image)ui(hx(x),hy(y),image->width*hudScale,image->height*hudScale,icon(*image).image);};
            auto centered=[&](const std::string& s,std::array<uint8_t,3> color,float x,float y) {
                if(!text16||s.empty())return;
                const auto& image=text16->get(s,color,true);at(&image,x-float(image.width)*.5f,y);
            };
            at(uiAssets->frame("bigmenu\\popupok",0),ox,oy);at(uiAssets->frame("bigmenu\\popupok",1),ox+256,oy);
            goldRegions.push_back({hx(ox),hy(oy),264*hudScale,176*hudScale,64,0,0,0});
            const uint32_t question=goldPopup.kind==GoldPopup::Kind::Drop?4033:goldPopup.kind==GoldPopup::Kind::Deposit?4049:4050;
            std::string words=strings?strings->byId(question):std::string();
            for(auto& c:words)if(c=='\n'||c=='\r')c=' ';
            // Wrapped to the box, a line every 16 pixels.
            std::vector<std::string> lines;std::string line,word;
            auto fits=[&](const std::string& t){return !text16||text16->font().width(t)<=220;};
            for(size_t i=0;i<=words.size();++i) {
                if(i<words.size()&&words[i]!=' '){word+=words[i];continue;}
                if(word.empty())continue;
                const std::string joined=line.empty()?word:line+" "+word;
                if(fits(joined))line=joined;else{lines.push_back(line);line=word;}
                word.clear();
            }
            if(!line.empty())lines.push_back(line);
            for(size_t i=0;i<lines.size();++i)centered(lines[i],white,ox+132,oy+40+16.f*float(i));
            centered(std::to_string(goldPopup.amount),white,ox+130,oy+134);
            auto button=[&](float x,int frame,int kind) {
                const bool down=leftButtonDown&&mouseX>=hx(x)&&mouseX<hx(x+15)&&mouseY>=hy(oy+136)&&mouseY<hy(oy+151);
                at(uiAssets->frame("menu\\goldbtn",uint32_t(frame+(down?1:0))),x,oy+136);
                goldRegions.push_back({hx(x),hy(oy+136),15*hudScale,15*hudScale,kind,0,0,0});
            };
            button(ox+56,0,62);button(ox+193,2,63);
        }
        const auto* areaInfo=levelTable?levelTable->byId(int32_t(worldView.level)):nullptr;
        // Without the game's panel art, name the area and show level and experience as text (the panel's experience bar does this otherwise).
        if(!uiAssets) {
            label(std::string(area!=areas.end()?area->second:areaInfo&&!areaInfo->name.empty()?areaInfo->name.c_str():"Unknown area"),16,10,235,215,140);
            label("Character level "+std::to_string(v.level)+"   Experience "+std::to_string(v.experience),16,30,200,200,200);
        }
        if(v.maxLife>0&&v.life<=0) {
            // The death screen (D2Client 0x6FB22320), while the player dies and lies dead (0x6FB29550: mode 0 or 17), centred
            // on the 800x600 screen: "You have died" (data\local\ui\eng\youdiedhardcore, two frames side by side) with its
            // bottom 94 above the middle, "Press ESC to continue." (youdiedinst) 48 lower; 48 lower again in font30, red, a
            // hardcore character's "Your deeds of valor will be remembered" (5096), else the gold lost, "Death takes its toll
            // of %d Gold" (5094, stat 175, when there is some); and in Nightmare and Hell (not hardcore) "You have lost
            // experience" (5095) 48 below that. Esc returns to town, or for a hardcore character leaves the game.
            const uint32_t mode=worldView.player().mode;
            if(mode==0||mode==17) {
                auto pair=[&](const char* file,float bottom) {
                    const auto* left=uiAssets?uiAssets->frame(file,0):nullptr;const auto* right=uiAssets?uiAssets->frame(file,1):nullptr;
                    if(!left||!right)return false;
                    const float x=400.f-float(left->width+right->width)*.5f,y=bottom-float(left->height);
                    ui(hx(x),hy(y),left->width*hudScale,left->height*hudScale,icon(*left).image);
                    ui(hx(x+float(left->width)),hy(y),right->width*hudScale,right->height*hudScale,icon(*right).image);
                    return true;
                };
                float bottom=300.f-94.f;
                const bool drawn=pair("data\\local\\ui\\eng\\youdiedhardcore",bottom)&pair("data\\local\\ui\\eng\\youdiedinst",bottom+48.f);
                if(!drawn)label("You have died - press Esc to continue",width*.5f,height*.3f,230,40,40,true);
                bottom+=96.f;
                auto line=[&](const std::string& words) {
                    if(words.empty())return;
                    if(!text30){label(words,width*.5f,hy(bottom-30.f),255,77,77,true);return;}
                    const auto& t=text30->get(words,red,true);
                    ui(hx(400.f-float(t.width)*.5f),hy(bottom-float(t.height)),t.width*hudScale,t.height*hudScale,&t);
                };
                if(strings) {
                    if(characterHardcore)line(strings->byId(5096));
                    else if(worldView.goldLost>0){std::string words=strings->byId(5094);if(const auto at=words.find("%d");at!=std::string::npos)words.replace(at,2,std::to_string(worldView.goldLost));line(words);}
                    if(gameDifficulty>0&&!characterHardcore){bottom+=48.f;line(strings->byId(5095));}
                }
            }
        }
        auto selectedEnemy=[&]() -> const d2::EntitySnapshot* {
            const float mx=(mouseX-width*.5f-panX)/zoom+scene->width*.5f,my=(mouseY-height*.5f-panY)/zoom+scene->height*.5f;
            const UnitPick* front=nullptr;
            for(const auto& box:unitPicks)if(box.kind==2&&mx>=box.x&&mx<=box.x+box.w&&my>=box.y&&my<=box.y+box.h&&(!front||box.depth>front->depth))front=&box;
            if(front)for(const auto& e:worldView.entities)if(e.id>=1000&&uint32_t(e.id-1000)==front->guid&&e.kind==d2::EntityKind::Enemy)return &e;
            return pickEnemy(mx,my);
        };
        if(const auto* enemy=selectedEnemy();enemy&&unitArt) {
            const auto* sprite=unitArt->sprite(enemy->classId);
            std::string name=sprite?(strings?strings->get(sprite->nameKey):sprite->id):"Monster";
            // Superuniques have their own name, random uniques one made of three parts ("Gloom" "touch" " the Hammer").
            if(strings&&(enemy->monsterType&2)&&enemy->name[0])name=strings->byId(enemy->name[0]);
            else if(strings&&(enemy->monsterType&8)&&!(enemy->monsterType&4)&&enemy->name[0])name=strings->byId(enemy->name[0])+strings->byId(enemy->name[1])+" "+strings->byId(enemy->name[2]);
            if(!name.empty())name[0]=char(std::toupper(static_cast<unsigned char>(name[0])));
            // As the game shows it: the name over a see-through box at the top centre, its life filling the box in red; uniques
            // in gold, champions in blue, the others white; the special abilities below (strings of monumod.txt's mods).
            const bool champion=(enemy->monsterType&4)&&!(enemy->monsterType&2),unique=!champion&&(enemy->monsterType&(2|8)); // champions have the unique flag too
            const auto& t=unique?text(name,199,179,119):champion?text(name,105,105,255):text(name,255,255,255);
            std::string abilities;
            if(strings)for(const uint8_t mod:enemy->umods) {
                static const std::map<uint8_t,uint32_t> modNames{{5,3205},{6,3206},{7,3207},{8,3208},{9,3209},{16,3220},{17,3211},{18,3210},{24,3217},
                    {25,3212},{26,3214},{27,3213},{28,3215},{29,3216},{30,3218},{36,22472},{37,22473},{38,22474},{39,22475}};
                if(const auto found=modNames.find(mod);found!=modNames.end())abilities+=(abilities.empty()?"":", ")+strings->byId(found->second);
            }
            if(!abilities.empty()) {
                const auto& a=text(abilities,255,255,255);
                const float s=std::min(float(width)/800.f,float(height)/600.f);
                ui(width*.5f-a.w*s*.5f,12*s+float(t.h)*s+10*s,a.w*s,a.h*s,a.image);
            }
            const float scale=std::min(float(width)/800.f,float(height)/600.f);
            const float w=std::max(float(t.w)*scale+24*scale,120*scale),h=float(t.h)*scale+6*scale,x=width*.5f-w*.5f,y=12*scale;
            const float life=enemy->maxLife>0?std::clamp(float(enemy->life)/enemy->maxLife,0.f,1.f):0;
            shade(x,y,w,h);
            if(life>0){d2::HudSprite bar;bar.color={180,0,0};bar.x=x;bar.y=y;bar.w=w*life;bar.h=h;bar.blend=d2::HudSprite::Blend::Mix;bar.amount=.6f;hud.push_back(bar);}
            ui(width*.5f-t.w*scale*.5f,y+3*scale,t.w*scale,t.h*scale,t.image);
        }
        // The in-game Esc menu is large gothic lettering over the darkened game,
        // with the animated red pentagrams from cursor/pentspin bracketing the selection.
        gameMenuRegions.clear();
        if(gameMenuPage!=GameMenuPage::Closed&&uiAssets&&text42) {
            {d2::HudSprite backdrop;backdrop.color={255,255,255};backdrop.w=float(width);backdrop.h=float(height);backdrop.blend=d2::HudSprite::Blend::Darken;hud.push_back(backdrop);}

            const auto buttons=gameMenuItems();
            // The game's spacing (60 pixels, three lines centred at 215); longer option lists close up below the heading.
            const size_t lines=buttons.size();
            const float gap=lines<=4?60.f:lines<=6?46.f:lines<=8?42.f:37.f;
            const float by=gameMenuPage==GameMenuPage::Main?215.f-gap*float(lines-1)*.5f:lines<=4?215.f:lines<=6?195.f:lines<=8?185.f:170.f;
            if(gameMenuPage==GameMenuPage::Options||gameMenuPage==GameMenuPage::Sound||gameMenuPage==GameMenuPage::Cheats) {
                const char* title=gameMenuPage==GameMenuPage::Cheats?"CHEATS":gameMenuPage==GameMenuPage::Sound?"SOUND":"OPTIONS";
                const auto& heading=text42->get(title,gold,true);
                ui(hx(400.f-heading.width*.5f),hy(145),heading.width*hudScale,heading.height*hudScale,icon(heading).image);
            }
            const auto selectorFrame=uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()/80)%8;
            const auto* selector=uiAssets->frame("cursor\\pentspin",selectorFrame);
            for(size_t i=0;i<buttons.size();++i) {
                const float y=by+gap*float(i);const auto [action,caption]=buttons[i];
                const auto& words=text42->get(caption,gold,true);
                const float textX=400.f-words.width*.5f;
                ui(hx(textX),hy(y),words.width*hudScale,words.height*hudScale,icon(words).image);
                if(int(i)==gameMenuSelection&&selector) {
                    const float selectorY=y+(float(words.height)-selector->height)*.5f;
                    sprite(selector,textX-selector->width-28.f,selectorY);
                    sprite(selector,textX+words.width+28.f,selectorY);
                }
                gameMenuRegions.push_back({hx(100),hy(y-7),600.f*hudScale,(float(words.height)+14.f)*hudScale,action});
            }
        }
    }
