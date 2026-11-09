#include "Tables.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static uint64_t fingerprint(const d2::Animation& animation) {
    uint64_t value=14695981039346656037ull;
    for(const auto& frame:animation.frames)for(auto byte:frame.image.rgba)value=(value^byte)*1099511628211ull;
    return value;
}
int main(int argc,char** argv){try {
    require(argc==2,"Usage: UnitArtBudgetTests GAME_DIR");
    d2::Archives archives(argv[1]);
    const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
    d2::UnitArt art(archives,palette);
    require(art.beginFrame(0,0)==0,"empty cache evicted something");
    const auto* zombie=art.get(5,1,0u);require(zombie&&!zombie->frames.empty(),"zombie missing");
    const auto expected=fingerprint(*zombie);
    require(art.get(5,1,0u)==zombie,"same-frame pointer changed");
    const auto* exact=art.get(5,1,std::array<uint8_t,16>{},0);require(exact&&!exact->frames.empty(),"exact appearance missing");
    d2::CharacterAppearance look;look.token="ba";
    const auto* player=art.player(look,1);require(player&&!player->frames.empty(),"player appearance missing");
    require(!art.get(1000000,1,0u),"unknown class has art");
    const auto loaded=art.cacheStats();require(loaded.entries==4&&loaded.bytes>0,"cache accounting missing");
    size_t callbacks=0;
    auto beforeEvict=[&](const d2::Animation& animation){require(!animation.frames.empty()&&!animation.frames.front().image.rgba.empty(),"frames freed before texture release");++callbacks;};
    require(art.beginFrame(0,0,beforeEvict)==0,"previous-frame art was not pinned");
    require(fingerprint(*zombie)==expected,"pinned art changed");
    require(art.beginFrame(0,0,beforeEvict)==4,"cold caches were not all trimmed");
    require(callbacks==3,"texture release count mismatch (missing entries have no textures)");
    require(art.cacheStats().entries==0&&art.cacheStats().bytes==0,"cache bytes retained after eviction");
    const auto* restored=art.get(5,1,0u);require(restored&&fingerprint(*restored)==expected,"reloaded pixels changed");
    for(int frame=0;frame<6;++frame) {
        require(art.get(5,1,0u)==restored,"active pointer changed");
        require(art.beginFrame(0,0,beforeEvict)==0,"actively used animation thrashed");
    }
    std::cout<<"PASS: "<<loaded.bytes<<" cached bytes; frame pinning, all three caches, missing-entry eviction, release-before-free and identical reloaded pixels\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
