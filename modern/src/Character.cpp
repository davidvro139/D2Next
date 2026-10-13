// COF layout and direction mapping adapted from Diablerie (MIT).
// See ../external/Diablerie-NOTICE.md.
#include "Assets.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace d2 {
Animation loadCharacter(const Archives& archives,const Palette& palette,const std::string& mode) {
    return loadCharacter(archives,palette,mode,CharacterAppearance{},"chars");
}
Animation loadCharacter(const Archives& archives,const Palette& palette,const std::string& mode,const CharacterAppearance& appearance,const std::string& root) {
    auto valid=[](const std::string& s,size_t length){return s.size()==length&&std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');});};
    if(!valid(appearance.token,2)||!valid(mode,2)||!valid(appearance.weaponClass,3))throw std::runtime_error("Invalid character animation tokens");
    for(const auto& component:appearance.components)if(!component.empty()&&!valid(component,3))throw std::runtime_error("Invalid character component token");
    const auto& token=appearance.token;
    const std::string base="data\\global\\"+root+"\\"+token+"\\";
    auto cof=archives.read(base+"cof\\"+token+mode+appearance.weaponClass+".cof");
    if(cof.size()<28)throw std::runtime_error("Truncated COF header");
    const unsigned layers=cof[0],frames=cof[1],dirs=cof[2];
    const bool monster=root!="chars";
    if(!layers||layers>16||!frames||!(dirs==8||dirs==16||(monster&&(dirs==1||dirs==4))))throw std::runtime_error("Unsupported character COF counts");
    const size_t order=28+9*layers+frames;
    if(order+size_t(dirs)*frames*layers>cof.size())throw std::runtime_error("Truncated COF layer order");
    constexpr const char* names[]={"hd","tr","lg","ra","la","rh","lh","sh","s1","s2","s3","s4","s5","s6","s7","s8"};
    std::array<Animation,16> parts;std::array<bool,16> present{};size_t layerBytes=0;
    for(unsigned i=0;i<layers;++i) {
        size_t p=28+9*i;const auto id=cof[p];
        if(id>=16||present[id])throw std::runtime_error("Invalid COF component");present[id]=true;
        if(appearance.components[id].empty())continue;

        std::string weapon(cof.begin()+p+5,cof.begin()+p+8);
        const std::string path=base+names[id]+"\\"+token+names[id]+appearance.components[id]+mode+weapon+".dcc";
        std::cout<<"Character layer: "<<path<<"\n";
        // Some layers have no file for some gear and stances (a new Necromancer's s3 armour, an Assassin's buckler with a
        // katar); the game draws nothing there, so the layer is left out.
        Bytes file;
        try {file=archives.read(path);}
        catch(const std::exception&) {std::cout<<"Character layer missing, not drawn: "<<path<<"\n";continue;}
        // Coloured gear: the layer through its item's colour map (dataglobalitemspalette<Transform file>.dat, the colour's
        // 256 indices), as D2Client draws a component with ITEMS_GetColor's map.
        Palette shifted=palette;
        if(const uint8_t trans=appearance.trans[id];trans>0&&trans<9&&appearance.colors[id]<21) {
            static const char* files[9]={nullptr,"grey","grey2","gold","brown","greybrown","invgrey","invgrey2","invgreybrown"};
            try {
                const auto maps=archives.read(std::string("data\\global\\items\\palette\\")+files[trans]+".dat");
                const size_t at=size_t(appearance.colors[id])*256;
                if(at+256<=maps.size())for(size_t i=0;i<256;++i)shifted[i]=palette[maps[at+i]];
            } catch(const std::exception&) {}
        }
        parts[id]=decodeDcc(file,shifted);
        for(const auto& frame:parts[id].frames)layerBytes+=frame.image.rgba.size();
        if(layerBytes>256*1024*1024)throw std::runtime_error("Character layers exceed 256 MiB");
        // A layer with fewer frames than the COF (a weapon's layer in a mode it is not meant for) holds its last frame. A
        // layer may also have fewer directions than the COF (the Raven: COF 16, its body 8); the game reads each layer with
        // its own count, so each of our directions takes that layer's nearest one (below).
        if(parts[id].directions>dirs||!parts[id].directions||dirs%parts[id].directions||!parts[id].framesPerDirection)
            throw std::runtime_error("Character DCC/COF count mismatch: "+path+" has "+std::to_string(parts[id].directions)+" directions x "+std::to_string(parts[id].framesPerDirection)+
                " frames, the COF "+std::to_string(dirs)+" x "+std::to_string(frames));
    }
    constexpr unsigned map8[]={4,0,5,1,6,2,7,3};
    constexpr unsigned map16[]={4,8,0,9,5,10,1,11,6,12,2,13,7,14,3,15};
    Animation result{dirs,frames,{}};size_t budget=0;
    for(unsigned d=0;d<dirs;++d)for(unsigned f=0;f<frames;++f) {
        unsigned internal=dirs==16?map16[d]:dirs==8?map8[d]:dirs==4?d:0;
        int left=0,top=0,right=1,bottom=1;
        auto layerFrame=[&](const Animation& part)->const AnimationFrame&{
            // A layer with fewer directions: our direction d of dirs is its direction d*its/dirs (the same sector of the
            // circle, from screen-down), in its own file order. Which of two neighbours 1.10f picks at an odd one isn't checked.
            const unsigned own=part.directions==dirs?internal:fileDirection(part.directions,d*part.directions/dirs);
            return part.frames[own*part.framesPerDirection+std::min(f,part.framesPerDirection-1)];
        };
        for(const auto& part:parts)if(!part.frames.empty()) {
            const auto& a=layerFrame(part);
            left=std::min(left,a.x);top=std::min(top,a.y);
            right=std::max(right,a.x+int(a.image.width));bottom=std::max(bottom,a.y+int(a.image.height));
        }
        unsigned w=right-left,h=bottom-top;budget+=size_t(w)*h*4;
        if(budget>128*1024*1024)throw std::runtime_error("Character frames exceed 128 MiB");
        AnimationFrame output{{w,h,Bytes(size_t(w)*h*4,0)},left,top};std::array<bool,16> used{};
        for(unsigned l=0;l<layers;++l) {
            unsigned id=cof[order+(d*frames+f)*layers+l];
            if(id>=16||!present[id]||used[id])throw std::runtime_error("Invalid COF layer ordering");used[id]=true;
            if(parts[id].frames.empty())continue;
            const auto& a=layerFrame(parts[id]);
            // A see-through layer (transparent flag, any effect but 5 "normal") goes to the glow picture; a solid layer drawn
            // after it hides what of it lies beneath.
            const unsigned layerAt=28+9*[&]{for(unsigned i=0;i<layers;++i)if(cof[28+9*i]==id)return i;return 0u;}();
            const bool seeThrough=cof[layerAt+3]&&cof[layerAt+4]!=5;
            const bool castsShadow=cof[layerAt+1]&&!seeThrough;
            if(castsShadow&&output.shadowMask.rgba.empty())output.shadowMask={w,h,Bytes(size_t(w)*h*4,0)};
            if(seeThrough&&output.glow.rgba.empty()){output.glow={w,h,Bytes(size_t(w)*h*4,0)};}
            if(seeThrough)output.glowEffect=cof[layerAt+4];
            for(unsigned y=0;y<a.image.height;++y)for(unsigned x=0;x<a.image.width;++x) {
                size_t src=(size_t(y)*a.image.width+x)*4;if(!a.image.rgba[src+3])continue;
                size_t dst=(size_t(y+a.y-top)*w+x+a.x-left)*4;
                if(seeThrough)std::copy_n(a.image.rgba.begin()+src,4,output.glow.rgba.begin()+dst);
                else {
                    std::copy_n(a.image.rgba.begin()+src,4,output.image.rgba.begin()+dst);
                    if(!output.glow.rgba.empty())output.glow.rgba[dst+3]=0;
                    if(castsShadow)output.shadowMask.rgba[dst+3]=255;
                }
            }
        }
        if(!output.shadowMask.rgba.empty()) {
            // Most frames cast the shadow of the whole picture; keep a separate mask only when some layers cast none.
            bool whole=true;
            for(size_t i=3;i<output.image.rgba.size()&&whole;i+=4)whole=(output.image.rgba[i]!=0)==(output.shadowMask.rgba[i]!=0);
            output.shadow=whole?1:2;
            if(whole)output.shadowMask={};
        }
        result.frames.push_back(std::move(output));
    }
    return result;
}
unsigned fileDirection(unsigned dirs,unsigned d) {
    // 8 directions are stored S-W, N-W, N-E, S-E, S, W, N, E; every doubling appends the in-between directions in order.
    constexpr unsigned map8[]={4,0,5,1,6,2,7,3};
    if(dirs<=1)return 0;
    // 4 directions are the diagonals S-W, N-W, N-E, S-E, in turn: our quarter d is file direction d. D2Client 1.10f's
    // 64-to-4 table (file offset 0xCEAD0) centres them on 64-directions 0, 16, 32, 48, the even ones of D2Common's 8.
    // (map8[d*2]>>1 gave 2,2,3,3: two quarters showed one direction, and reordering emptied a frame.)
    if(dirs==4)return d%4;
    if(dirs==8)return map8[d%8];
    d%=dirs;
    return d%2?dirs/2+d/2:fileDirection(dirs/2,d/2);
}
Animation inScreenOrder(Animation animation) {
    if(animation.directions<=1)return animation;
    std::vector<AnimationFrame> frames(animation.frames.size());
    const auto per=animation.framesPerDirection;
    for(unsigned d=0;d<animation.directions;++d)for(unsigned f=0;f<per;++f)
        frames[size_t(d)*per+f]=std::move(animation.frames[size_t(fileDirection(animation.directions,d))*per+f]);
    animation.frames=std::move(frames);
    return animation;
}
}
