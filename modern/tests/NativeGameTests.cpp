#include "NativeGame.h"
#include "NativeTown.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static d2::Bytes readSave(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeGameTests DLL_DIR GAME_DIR SAVE");
    const auto original=readSave(argv[3]);require(!original.empty(),"save missing");
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678);
    std::vector<d2::WorldSnapshot> firstRun;
    for(int repeat=0;repeat<2;++repeat) {
        auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin);
        if(repeat==0&&!session.coloredEquipment) {
            const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
            for(const char* mode:{"tn","tw","nu","wl","rn"}) {
                auto animation=d2::loadCharacter(archives,palette,mode,session.appearance);
                require(animation.directions==16&&!animation.frames.empty(),"native appearance has no directional animation");
                for(const auto& frame:animation.frames) {
                    size_t opaque=0;for(size_t i=3;i<frame.image.rgba.size();i+=4)opaque+=frame.image.rgba[i]!=0;
                    require(opaque>100,"empty native character animation frame");
                }
                require(animation.frames[0].image.rgba!=animation.frames[1].image.rgba,"native character animation does not change");
                // Removing each equipped hand/shield layer must alter actual pixels.
                for(size_t component:{5u,6u,7u})if(!session.appearance.components[component].empty()) {
                    auto stripped=session.appearance;stripped.components[component].clear();
                    const auto without=d2::loadCharacter(archives,palette,mode,stripped);
                    bool differs=false;
                    for(size_t i=0;i<animation.frames.size();++i)differs|=animation.frames[i].image.rgba!=without.frames[i].image.rgba;
                    require(differs,"equipped weapon/shield did not affect rendered frames");
                }
            }
            auto invalid=session.appearance;invalid.components[0]="../";
            bool rejected=false;try{d2::loadCharacter(archives,palette,"tn",invalid);}catch(const std::exception&){rejected=true;}
            require(rejected,"invalid appearance token accepted");
        }
        auto& backend=*session.backend;auto initial=backend.snapshot();const auto id=initial.localPlayer;
        {
            // The Rogue Encampment's people are native monsters: Gheed (147), Akara (148), Charsi (154), Kashya (150), Warriv (155).
            int townPeople=0;bool gheed=false,akara=false;
            for(const auto& e:initial.entities) {
                if(e.id==id)continue;
                require(e.id>=1000&&e.kind!=d2::EntityKind::Player,"unit entity has a player id or kind");
                if(e.kind==d2::EntityKind::TownPerson){++townPeople;gheed|=e.classId==147;akara|=e.classId==148;}
            }
            require(townPeople>=4&&gheed&&akara,"native town people missing from snapshot");
        }
        require(backend.apply(0,d2::Command::stop())==d2::CommandResult::UnknownEntity,"invalid actor accepted");
        require(backend.apply(id,d2::Command::moveTo({-1,-1}))==d2::CommandResult::Rejected,"invalid destination accepted");
        require(backend.snapshot()==initial,"rejected input changed snapshot");
        const auto start=d2::subtileOf(initial.player().position);auto destination=start;
        for(int y=-5;y<=5&&destination==start;++y)for(int x=-5;x<=5;++x) {
            d2::GridPoint p{start.x+x,start.y+y};const auto path=d2::findPath(town.collision,start,p);
            if(path.size()>=3&&path.size()<=7){destination=p;break;}
        }
        require(destination!=start,"no nearby movement target");
        require(backend.apply(id,d2::Command::moveTo(destination))==d2::CommandResult::Accepted,"walk request rejected");
        bool moved=false;std::set<uint32_t> walkFrames;
        for(int tick=0;tick<100;++tick) {
            backend.tick();auto view=backend.snapshot();moved|=view.player().moving;
            if(view.player().mode==2||view.player().mode==6)walkFrames.insert(view.player().animationFrame);
            require(view.tick==uint64_t(tick+1)&&view.localPlayer==id,"unstable tick/identity");
            if(repeat==0)firstRun.push_back(view);else require(view==firstRun[tick],"native command replay differs");
        }
        require(moved&&d2::subtileOf(backend.snapshot().player().position)==destination,"native walk did not arrive");
        require(walkFrames.size()>1,"native walking animation frame does not advance");
        require(backend.apply(id,d2::Command::setRun(true))==d2::CommandResult::Accepted,"run toggle rejected");
        backend.apply(id,d2::Command::moveTo(start));
        std::set<uint32_t> runFrames;
        for(int tick=0;tick<100;++tick){backend.tick();const auto p=backend.snapshot().player();if(p.mode==3)runFrames.insert(p.animationFrame);}
        require(d2::subtileOf(backend.snapshot().player().position)==start,"native run did not arrive");
        require(runFrames.size()>1,"native running mode/frame does not advance");
        backend.apply(id,d2::Command::setRun(false));
        backend.apply(id,d2::Command::moveTo(destination));
        for(int tick=0;tick<100;++tick)backend.tick();
        require(initial.player().position==d2::subtileCenter(start),"initial snapshot mutated or unexpected native centre");
        // Held movement and release must settle and remain stable without input.
        auto before=backend.snapshot().player().position;
        d2::GridPoint held{};
        for(d2::GridPoint direction: {d2::GridPoint{1,0},{-1,0},{0,1},{0,-1}})
            if(town.collision.canStep(destination,{destination.x+direction.x,destination.y+direction.y})){held=direction;break;}
        require(held!=d2::GridPoint{},"no held movement direction");
        backend.apply(id,d2::Command::hold(held.x,held.y));for(int i=0;i<10;++i)backend.tick();
        require(backend.snapshot().player().position!=before,"held direction did not move native player");
        backend.apply(id,d2::Command::hold(0,0));for(int i=0;i<20;++i)backend.tick();
        const auto rest=backend.snapshot().player().position;
        for(int i=0;i<10;++i)backend.tick();
        require(backend.snapshot().player().position==rest,"native player did not stop on release");
        session.backend.reset();require(initial.player().id==id,"retained snapshot lost");
        require(readSave(argv[3])==original,"original save changed");
    }
    bool failed=false;
    try{auto invalid=d2::startNativeGame(argv[1],argv[2],std::filesystem::path(argv[3]).wstring()+L".missing",town.seed,town.collision,town.worldOrigin);}
    catch(const std::exception&){failed=true;}
    require(failed,"missing save did not fail");
    std::cout<<"PASS: native appearance layers, idle/walk frames, join, authoritative walking, held movement/release, replay, repeated sessions, missing-save failure and unchanged original save\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
