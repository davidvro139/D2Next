// Prints where each frame of a character mode's art sits (offset from the feet, and size) for one direction: which
// frames of a skill's animation are off the ground, e.g. the Barbarian's Leap (S1).
// SequenceArtProbe <game dir> <class token, e.g. ba> <mode, e.g. S1> [weapon class, default hth] [direction 0-15]
#include "Assets.h"
#include <iostream>
#include <string>
#include <algorithm>
int main(int argc,char** argv){try {
    if(argc<4)return 2;
    d2::Archives archives(argv[1]);
    d2::Palette palette{};for(int i=0;i<256;++i)palette[size_t(i)]={uint8_t(i),uint8_t(i),uint8_t(i),255};
    d2::CharacterAppearance look;look.token=argv[2];look.weaponClass=argc>4?argv[4]:"hth";
    const auto animation=d2::loadCharacter(archives,palette,argv[3],look);
    const unsigned direction=argc>5?unsigned(std::stoi(argv[5])):0;
    std::cout<<argv[2]<<" "<<argv[3]<<" "<<look.weaponClass<<": "<<animation.directions<<" directions, "<<animation.framesPerDirection<<" frames\n";
    for(unsigned f=0;f<animation.framesPerDirection;++f) {
        const auto& frame=animation.frames[(direction*animation.directions/16)*animation.framesPerDirection+f];
        // The rows actually drawn (alpha above zero), relative to the feet.
        int top=1<<30,bottom=-(1<<30);
        for(uint32_t y=0;y<frame.image.height;++y)for(uint32_t x=0;x<frame.image.width;++x)
            if(frame.image.rgba[(size_t(y)*frame.image.width+x)*4+3]){top=std::min(top,frame.y+int(y));bottom=std::max(bottom,frame.y+int(y));}
        std::cout<<"frame "<<f<<": drawn rows "<<top<<" to "<<bottom<<" (0 = the feet; negative is up)\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
