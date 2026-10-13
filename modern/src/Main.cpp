// D2Next: startup (arguments, game data, the menus), the game loop, and the scripted test runs.
#include "Renderer.h"
#include "NativeProtocol.h"
#include "SkillVisuals.h"
#include "NpcTopics.h"
#include "ProgramPath.h"
#include "GpuSelect.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <list>
#include <regex>
#include <random>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdio>
#define WEB_STATIC static
static std::function<void()> gWebFrame;
static void webFrameTrampoline() {
    try { if(gWebFrame)gWebFrame(); }
    catch(const std::exception& e) { std::printf("Web frame: %s\n",e.what()); std::fflush(stdout); }
}
// The browser cannot wait in a loop, so the menus run one frame at a time too (emscripten_set_main_loop unwinds the
// stack). Once a character is chosen run() starts again with it (webChosen) and the front end's loading screen (webFront);
// Back to Main Menu starts run() again the same way. The arguments are kept here for those restarts.
static int run(int argc,wchar_t** argv);
static std::vector<std::wstring> webArgs;
static std::vector<wchar_t*> webArgPointers;
static std::optional<std::filesystem::path> webChosen;
static std::unique_ptr<d2::FrontEnd> webFront;
static int webRun() {return run(int(webArgPointers.size())-1,webArgPointers.data());}
// The page is told when the game has ended (the player quit from the menus), so it can show itself again.
static void webGameEnded() {EM_ASM({ if (Module.onGameExit) Module.onGameExit(); });}
#else
#define WEB_STATIC
#endif
// Give the browser a chance to paint and handle input. Desktop vsync already waits in SwapWindow.
static void yieldFrame(int milliseconds) {
#ifdef __EMSCRIPTEN__
    (void)milliseconds; // the browser main loop yields between frames
#else
    if(milliseconds>0)std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}
// The player's animations for a look: town neutral and walk, and (with the native game) every other mode the class has art
// for. Death and dead exist only unarmed (hth).
struct LookSet {d2::Animation idle,walking;std::map<uint8_t,d2::Animation> movement;};
static LookSet buildLook(const d2::Archives& archives,const d2::Palette& palette,const d2::CharacterAppearance& look,bool allModes) {
    LookSet set;
    set.idle=d2::loadCharacter(archives,palette,d2::playerModeTokens[5],look);
    set.walking=d2::loadCharacter(archives,palette,d2::playerModeTokens[6],look);
    if(!allModes)return set;
    for(uint8_t mode=0;mode<20;++mode) {
        if(mode==5||mode==6)continue;
        auto modeLook=look;
        if(mode==0||mode==17)modeLook.weaponClass="hth";
        try {set.movement.emplace(mode,d2::loadCharacter(archives,palette,d2::playerModeTokens[mode],modeLook));}
        catch(const std::exception&) {} // the class has no such animation with this weapon
    }
    return set;
}
#ifndef __EMSCRIPTEN__
// Builds a new look on its own thread, with its own archives (StormLib handles are not shared between threads), while the
// game goes on drawing the old one; the newest request wins.
class LookBuilder {
public:
    struct Result {std::array<uint32_t,17> key{};d2::CharacterAppearance look;LookSet set;bool failed=false;std::string error;};
    LookBuilder(std::filesystem::path root,d2::Palette palette,bool allModes):root_(std::move(root)),palette_(palette),allModes_(allModes),thread_([this]{run();}){}
    ~LookBuilder(){{std::lock_guard lock(mutex_);stop_=true;}wake_.notify_all();thread_.join();}
    void request(const std::array<uint32_t,17>& key,const d2::CharacterAppearance& look){{std::lock_guard lock(mutex_);job_=Result{key,look};}wake_.notify_all();}
    std::optional<Result> take(){std::lock_guard lock(mutex_);auto done=std::move(done_);done_.reset();return done;}
private:
    void run() {
        // The archives are opened at once (most of a first build's time), so the first new look comes quickly.
        std::unique_ptr<d2::Archives> archives;
        try{archives=std::make_unique<d2::Archives>(root_);}catch(const std::exception&){}
        for(;;) {
            Result job;
            {std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stop_||job_.has_value();});if(stop_)return;job=std::move(*job_);job_.reset();}
            try {
                if(!archives)archives=std::make_unique<d2::Archives>(root_);
                job.set=buildLook(*archives,palette_,job.look,allModes_);
            } catch(const std::exception& e){job.failed=true;job.error=e.what();}
            std::lock_guard lock(mutex_);done_=std::move(job);
        }
    }
    std::filesystem::path root_;d2::Palette palette_;bool allModes_;
    std::mutex mutex_;std::condition_variable wake_;std::optional<Result> job_,done_;bool stop_=false;
    std::thread thread_;
};
#endif
// A window point for scripted clicks (--click X Y).
struct ClickPoint {int x=0,y=0;};
// A scripted left click (tests and captures), as the window would report it.
static void scriptedClick(int x,int y,unsigned button=0) {
    d2::InputEvent e;e.type=d2::InputEvent::Type::ButtonDown;e.x=x;e.y=y;e.button=button;handleInput(e);
    e.type=d2::InputEvent::Type::ButtonUp;handleInput(e);
}

// After Back to Main Menu (gameMain runs this again): the character just played, chosen first in character select.
static std::filesystem::path lastPlayed;
static bool backFromGame=false;
// A TCP/IP game from the menu that could not start: the menus open again on the TCP/IP screen with the failure reason.
static std::optional<std::string> lanFailure;
static int run(int argc,wchar_t** argv) {
    try {
        static_assert(sizeof(void*)==8);
#ifdef __EMSCRIPTEN__
        if(webArgs.empty()) {
            for(int i=0;i<argc;++i)webArgs.push_back(argv[i]);
            for(auto& a:webArgs)webArgPointers.push_back(a.data());
            webArgPointers.push_back(nullptr);
        }
#endif
        d2::installCrashReport();
        WEB_STATIC std::filesystem::path root,capture,nativeDlls,nativeSave,profileDir;
        root=defaultGameDirectory();capture.clear();nativeDlls.clear();nativeSave.clear();profileDir.clear();
        WEB_STATIC d2::GridPoint nativeOrigin{};WEB_STATIC std::vector<int> sceneComplete;sceneComplete={1,2,3,4}; // levels whose surroundings are all in the loaded scene
        WEB_STATIC d2::CharacterAppearance appearance;
        WEB_STATIC uint32_t nativeSeed=0x12345678;bool seedGiven=false;nativeSeed=0x12345678;
        WEB_STATIC std::string asset;asset="data\\global\\items\\invhax.dc6";
        bool skipMenu=false;
        bool allWaypoints=true,noAudio=false,autoplay=false,talkShop=false,wantSkillMenu=false,wantWaypoint=false;std::vector<ClickPoint> clicks,lateClicks;int autoplayTicks=3000;uint32_t talkClass=0;uint32_t castSkillId=0,travelLevel=0;bool walkWorld=false;int walkWorldX=0,walkWorldY=0;bool smoke=false,software=false,verify=false;
        WEB_STATIC bool singleTile=false,referenceMap=false,markerOnly=false;singleTile=false;referenceMap=false;markerOnly=false;
        bool walk=false,mapStats=false;
        uint64_t captureTick=0;bool touchArgument=false;
        bool captureWalking=false,havePlace=false,openEscMenu=false;unsigned captureFacing=0;
        d2::GridPoint placeAt;
        float requestedZoom=0;
        if(argc==1&&d2::nativeGameLinkedIn())nativeDlls=L"(linked in)";
        for(int i=1;i<argc;++i) {
            std::wstring arg=argv[i];
            if(arg==L"--game-dir"&&i+1<argc) root=argv[++i];
            else if(arg==L"--native-town"&&i+1<argc) nativeDlls=argv[++i];
            else if(arg==L"--native") {
                // The game linked into this program (the in-process build); the DLL build needs --native-town DLL_DIR.
                if(!d2::nativeGameLinkedIn())throw std::runtime_error("This build runs the game from the x64 DLLs: use --native-town DLL_DIR");
                nativeDlls=L"(linked in)";
            }
            else if(arg==L"--native-save"&&i+1<argc) nativeSave=argv[++i];
            else if(arg==L"--profile"&&i+1<argc) profileDir=argv[++i];
            else if(arg==L"--no-audio") noAudio=true;
            else if(arg==L"--skip-menu") skipMenu=true;
            else if(arg==L"--no-waypoints") allWaypoints=false;
            else if(arg==L"--seed"&&i+1<argc) {
                size_t used=0;const std::wstring value=argv[++i];
                auto seed=std::stoull(value,&used,0);
                if(used!=value.size()||seed>UINT32_MAX||value.starts_with(L"-"))throw std::runtime_error("Seed must be an unsigned 32-bit integer");
                nativeSeed=uint32_t(seed);seedGiven=true;
            }
            else if(arg==L"--tiles") asset="data\\global\\tiles\\act1\\town\\floor.dt1";
            else if(arg==L"--map") asset="data\\global\\tiles\\act1\\town\\townn1.ds1";
            else if(arg==L"--single-tile") singleTile=true;
            else if(arg==L"--reference-map") referenceMap=true;
            else if(arg==L"--walk")walk=true;
            else if(arg==L"--marker")markerOnly=true;
            else if(arg==L"--map-stats")mapStats=true;            else if(arg==L"--place"&&i+2<argc){placeAt=d2::GridPoint{std::stoi(argv[i+1]),std::stoi(argv[i+2])};i+=2;havePlace=true;}
            else if(arg==L"--animation-tick"&&i+1<argc)captureTick=std::stoull(argv[++i]);
            else if(arg==L"--animation-walk")captureWalking=true;
            else if(arg==L"--facing"&&i+1<argc){captureFacing=std::stoul(argv[++i]);if(captureFacing>=16)throw std::runtime_error("Facing must be 0 through 15");}
            else if(arg==L"--collision"){walk=true;showCollision=true;}
            else if(arg==L"--zoom"&&i+1<argc) {
                requestedZoom=std::stof(argv[++i]);
                if(!(requestedZoom>=.02f&&requestedZoom<=16))throw std::runtime_error("Zoom must be between 0.02 and 16");
            }
            else if(arg==L"--asset"&&i+1<argc) {
                std::wstring a=argv[++i]; asset.clear();
                for(wchar_t c:a) {
                    if(c>127) throw std::runtime_error("MPQ asset names must be ASCII");
                    asset.push_back(static_cast<char>(c));
                }
            }
            else if(arg==L"--capture"&&i+1<argc) capture=argv[++i];
            else if(arg==L"--software") software=true;
            else if(arg==L"--touch") touchArgument=true; // the phone controls on a desktop (TOUCH-CONTROLS.md)
            else if(arg==L"--smoke-test") smoke=true;
            else if(arg==L"--open-inventory")inventoryOpen=true;
            else if(arg==L"--open-skills")skillsOpen=true;
            else if(arg==L"--open-character")characterOpen=true;
            else if(arg==L"--open-esc-menu")openEscMenu=true;
            else if(arg==L"--skill-menu")wantSkillMenu=true;
            else if(arg==L"--talk"&&i+1<argc)talkClass=uint32_t(std::stoul(argv[++i]));
            else if(arg==L"--open-waypoint")wantWaypoint=true;
            else if(arg==L"--walk-world"&&i+2<argc){walkWorld=true;walkWorldX=std::stoi(argv[++i]);walkWorldY=std::stoi(argv[++i]);}
            else if(arg==L"--travel"&&i+1<argc)travelLevel=uint32_t(std::stoul(argv[++i]));
            else if(arg==L"--cast-skill"&&i+1<argc)castSkillId=uint32_t(std::stoul(argv[++i]));
            else if(arg==L"--shop")talkShop=true;
            else if(arg==L"--click-after"&&i+2<argc){lateClicks.push_back(ClickPoint{std::stoi(argv[i+1]),std::stoi(argv[i+2])});i+=2;}
            else if(arg==L"--click"&&i+2<argc){clicks.push_back(ClickPoint{std::stoi(argv[i+1]),std::stoi(argv[i+2])});i+=2;}
            else if(arg==L"--autoplay"&&i+1<argc){autoplay=true;autoplayTicks=std::stoi(argv[++i]);}
            else if(arg==L"--verify-assets") verify=true;
            // LAN play: host the game for other players (port, default 6112), or join a host's game.
            else if(arg==L"--lan-host") {
                std::string port="4000"; // the original TCP/IP port
                if(i+1<argc&&argv[i+1][0]>=L'0'&&argv[i+1][0]<=L'9'){port.clear();for(const wchar_t* c=argv[++i];*c;++c)port+=char(*c);}
                d2::setEnvironment("D2MODERN_LAN_HOST",port.c_str());
            }
            else if(arg==L"--lan-join"&&i+1<argc){std::string address;for(const wchar_t* c=argv[++i];*c;++c)address+=char(*c);d2::setEnvironment("D2MODERN_LAN_JOIN",address.c_str());}
            else {
                throw std::runtime_error("Usage: D2Next [--map | --tiles | --native | --native-town DLL_DIR] [--seed N] [--native-save SAVE] [--profile PATH] [--walk] [--marker] [--collision] [--place X Y] [--map-stats] [--reference-map] [--zoom SCALE] [--single-tile] [--game-dir PATH] [--asset MPQ_PATH] [--software] [--verify-assets] [--lan-host [PORT] | --lan-join ADDRESS[:PORT]] [--smoke-test] [--capture BMP] [--open-esc-menu] [--animation-tick N] [--animation-walk] [--facing 0..15]");
            }
        }
        if(openEscMenu)gameMenuPage=GameMenuPage::Main;
        std::filesystem::path profile=profileDir.empty()?defaultSaveDirectory():profileDir;
        std::error_code profileError;
        std::filesystem::create_directories(profile,profileError);
        if(profileError)throw std::runtime_error("Cannot create local save directory: "+profile.string());
        // The phone controls' settings (the Esc menu's options) live with the characters. Test runs keep the defaults.
        if(!smoke&&capture.empty())touch::loadSettings(profile/"settings.ini");
        if(const auto file=d2::environmentPath("D2MODERN_SETTINGS");!file.empty())touch::loadSettings(file); // test hook: this settings file, also in test runs
        if(touchArgument)touch::setEnabled(true);
        if(nativeSave.empty()&&!nativeDlls.empty()) {
            const auto localCharacters=d2::findCharacters({profile});
            if(!localCharacters.empty())nativeSave=localCharacters.front().path;
        }
        if(!nativeSave.empty()) {
            if(nativeDlls.empty()||referenceMap||havePlace)throw std::runtime_error("--native-save requires --native-town and does not support --reference-map or --place");
            walk=true;
        }
        if(!nativeDlls.empty())asset="data\\global\\tiles\\act1\\town\\townn1.ds1";
        WEB_STATIC d2::Archives archives(root);
        WEB_STATIC d2::Palette palette;palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        if(const char* pattern=std::getenv("D2MODERN_STRINGS_GREP")) {
            // Test hook: every string whose key matches this regular expression (case-insensitive): id, key, text.
            const d2::StringTable table(archives);const std::regex re(pattern,std::regex::icase);
            for(const auto& [id,key]:table.keysById())if(std::regex_search(key,re)) {
                auto text=table.byId(id);std::replace(text.begin(),text.end(),'\n','|');
                std::cout<<id<<" "<<key<<" = "<<text.substr(0,140)<<"\n";
            }
            return 0;
        }
        if(const char* spec=std::getenv("D2MODERN_TABLE_GREP")) {
            // Test hook: "archive path;regex" prints the table's header and every row whose text matches (tab separated).
            const std::string s=spec;const auto split=s.find(';');
            const auto bytes=archives.read(s.substr(0,split));const std::regex re(s.substr(split+1),std::regex::icase);
            std::istringstream lines(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));std::string line;bool first=true;
            while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(first||std::regex_search(line,re))std::cout<<line<<"\n";first=false;}
            return 0;
        }
        if(const char* paths=std::getenv("D2MODERN_ARCHIVE_HAS")) {
            // Test hook: "path;path;..." prints each archive file's size, or that it is missing.
            std::istringstream list(paths);std::string path;
            while(std::getline(list,path,';')){try{std::cout<<path<<": "<<archives.read(path).size()<<" bytes\n";}catch(...){std::cout<<path<<": missing\n";}}
            return 0;
        }
        if(const char* dump=std::getenv("D2MODERN_DUMP_DC6")) {
            // Test hook: "archive path;output prefix" writes each frame of a DC6 (act 1 palette) as prefix-N.bmp, with its offset.
            const std::string spec=dump;const auto split=spec.find(';');
            const auto bytes=archives.read(spec.substr(0,split));const std::string prefix=spec.substr(split+1);
            for(uint32_t f=0;f<d2::dc6FrameCount(bytes);++f) {
                const auto image=d2::decodeDc6(bytes,palette,f);const auto at=d2::dc6FrameOffset(bytes,f);
                std::cout<<"Frame "<<f<<": "<<image.width<<"x"<<image.height<<" offset "<<at[0]<<","<<at[1]<<"\n";
                std::ofstream out(prefix+"-"+std::to_string(f)+".bmp",std::ios::binary);
                const uint32_t w=image.width,h=image.height,size=54+w*h*4;uint8_t header[54]{'B','M'};
                std::memcpy(header+2,&size,4);header[10]=54;header[14]=40;std::memcpy(header+18,&w,4);const int32_t negH=-int32_t(h);std::memcpy(header+22,&negH,4);header[26]=1;header[28]=32;
                out.write(reinterpret_cast<const char*>(header),54);
                for(size_t p=0;p<size_t(w)*h;++p){const uint8_t* c=&image.rgba[p*4];const uint8_t px[4]={c[3]?c[2]:uint8_t(255),c[3]?c[1]:uint8_t(0),c[3]?c[0]:uint8_t(255),255};out.write(reinterpret_cast<const char*>(px),4);}
            }
            return 0;
        }
        if(std::getenv("D2MODERN_SCAN_FLAT_WALLS")) {
            // Test hook: every wall tile (not shadows or special tiles) of every lvltypes.txt tile file that is one flat colour;
            // the native town leaves those out (NativeTown.cpp).
            const auto text=archives.read("data\\global\\excel\\lvltypes.txt");
            const d2::TextTable types(std::string_view(reinterpret_cast<const char*>(text.data()),text.size()));
            std::set<std::string> seen;
            for(size_t r=0;r<types.rows();++r)for(int f=1;f<=32;++f) {
                auto file=types.cell(r,"File "+std::to_string(f));std::replace(file.begin(),file.end(),'/','\\');
                if(file.empty()||file=="0"||!seen.insert(file).second)continue;
                d2::Bytes bytes;try{bytes=archives.read("data\\global\\tiles\\"+file);}catch(...){std::cout<<"Unread: "<<file<<"\n";continue;}
                const auto count=d2::dt1TileCount(bytes);
                for(uint32_t i=0;i<count;++i) {
                    d2::Tile tile;try{tile=d2::decodeDt1(bytes,palette,i);}catch(...){continue;}
                    if(tile.orientation<1||tile.orientation>14||tile.orientation==13||tile.orientation==10||tile.orientation==11)continue;
                    bool flat=true,any=false;uint32_t first=0;
                    for(size_t p=0;p+3<tile.image.rgba.size()&&flat;p+=4){if(!tile.image.rgba[p+3])continue;uint32_t c;std::memcpy(&c,&tile.image.rgba[p],4);if(!any){first=c;any=true;}else flat=c==first;}
                    if(any&&flat)std::cout<<"Flat wall: "<<file<<" tile "<<i<<" o"<<tile.orientation<<" main "<<tile.mainIndex<<" sub "<<tile.subIndex<<" rarity "<<tile.rarity<<" "<<tile.image.width<<"x"<<tile.image.height<<"\n";
                }
            }
            std::cout<<"Scanned "<<seen.size()<<" tile files\n";
            return 0;
        }
        WEB_STATIC d2::UIAssets uiAssets(archives,palette);
        WEB_STATIC int activeAct=1;activeAct=1;
        auto actName=[](int act) {
            static constexpr const char* names[]={"I","II","III","IV","V"};
            return act>=1&&act<=5?std::string("Act ")+names[act-1]:std::string("Act ?");
        };
        WEB_STATIC std::function<d2::Palette()> loadActPalette;
        loadActPalette=[&] {
            return d2::decodePalette(archives.read("data\\global\\palette\\act"+std::to_string(activeAct)+"\\pal.dat"));
        };
        // The window lasts for the whole program: leaving a game for the main menu runs all of this again in it.
        static std::unique_ptr<d2::Window> window;
        if(!window)window=d2::makeWindow("Diablo II",944,601);
        gameWindow=window.get();
        // The window's drawable size, in the fields the camera code uses.
        struct ClientArea {int right=0,bottom=0;};
        WEB_STATIC std::function<ClientArea()> clientRect;
        clientRect=[&]{ClientArea area;window->clientSize(area.right,area.bottom);return area;};
        WEB_STATIC Renderer renderer(d2::makeGpuBackend(*window,software));
        renderer.uiAssets=&uiAssets; // the menus use its cursor too
        WEB_STATIC std::unique_ptr<d2::Audio> audio; // created for real play only (not in captures and tests)
        auto applyAudioSettings=[&] {
            if(!audio)return;
            const auto& s=touch::settings();
            audio->setMusicEnabled(s.music&&s.musicVolume>0);
            audio->setMusicVolume(float(s.musicVolume)/10.f);
            audio->setEffectsEnabled(s.effects&&s.effectsVolume>0);
            audio->setEffectsVolume(float(s.effectsVolume)/10.f);
        };
        bool loadingClosed=false;
        std::unique_ptr<d2::FrontEnd> loadingFront;
        auto loadingLast=std::chrono::steady_clock::now();
        auto loadingDrawLast=loadingLast-std::chrono::milliseconds(33);
        auto pumpLoading=[&] {
            if(!loadingFront||loadingFront->screen()!=d2::FrontEnd::Screen::Loading)return;
            for(d2::InputEvent e;window->pollEvent(e);)if(e.type==d2::InputEvent::Type::Quit)loadingClosed=true;
            const auto now=std::chrono::steady_clock::now();
            loadingFront->advance(std::chrono::duration<double>(now-loadingLast).count());
            loadingLast=now;
            if(now-loadingDrawLast<std::chrono::milliseconds(33))return;
            loadingDrawLast=now;
            if(renderer.drawScreen(loadingFront->draw()))renderer.present();
            // No yield: startNativeGame calls this with D2Game on the stack.
        };
        // Player files and appearance data live in the local Save folder unless --profile selects another folder.
        // Playing starts at the title screen, main menu and character select; the game is loaded only once a character is chosen.
        // Tests, captures and scripted runs (and --skip-menu) go straight into the game with --native-save.
        bool menuLan=false; // a TCP/IP game chosen in the menus (one that fails to start goes back to them)
        if(const char* shot=std::getenv("D2MODERN_FRONT_SHOT");shot&&*shot&&!nativeSave.empty()) {
            // Test hook: character select with --native-save's character chosen and Enter pressed (the difficulty popup when it
            // has opened Nightmare), D2MODERN_FRONT_KEYS more keys after (letters as virtual keys), captured to this BMP.
            d2::FrontEnd front(archives,d2::findCharacters({nativeSave.parent_path()}),{profile},nativeSave,profile);
            if(const char* credits=std::getenv("D2MODERN_FRONT_CREDITS")){front.showCredits();front.advance(std::atof(credits));}
            else{front.showCharacterSelect();front.key(d2::key::Enter);}
            if(const char* keys=std::getenv("D2MODERN_FRONT_KEYS"))for(const char* k=keys;*k;++k)front.key(unsigned(*k));
            if(const char* click=std::getenv("D2MODERN_FRONT_CLICK")){float x=0,y=0;if(std::sscanf(click,"%f,%f",&x,&y)==2){front.mouseMove(x,y);front.mouseDown(x,y);front.mouseUp(x,y);}}
            front.advance(.5);
            if(renderer.drawScreen(front.draw()))renderer.readback(shot);
            std::cout<<"Front end: screen "<<int(front.screen())<<", difficulty "<<front.difficulty()<<std::endl;
            return 0;
        }
        if(!nativeDlls.empty()&&!smoke&&capture.empty()&&(!skipMenu||backFromGame)&&clicks.empty()&&lateClicks.empty()&&!autoplay) {
            std::filesystem::path chosen;
#ifdef __EMSCRIPTEN__
            if(webChosen) {
                // The browser's menus chose this character in an earlier frame (below); their loading screen stays up.
                chosen=*webChosen;webChosen.reset();
                loadingFront=std::move(webFront);frontEnd=loadingFront.get();
                loadingLast=std::chrono::steady_clock::now();loadingDrawLast=loadingLast-std::chrono::milliseconds(33);
                pumpLoading();
            } else
#endif
            {
                if(backFromGame&&!lastPlayed.empty())nativeSave=lastPlayed;
                if(!noAudio){audio=std::make_unique<d2::Audio>(archives);applyAudioSettings();}
                std::vector<std::filesystem::path> saveFolders{profile};
                if(!nativeSave.empty())saveFolders.insert(saveFolders.begin(),nativeSave.parent_path());
                loadingFront=std::make_unique<d2::FrontEnd>(archives,d2::findCharacters(saveFolders),std::vector<std::filesystem::path>{profile},nativeSave,profile);
                if(audio)loadingFront->setMovieAudioPlayer([&](d2::Bytes pcm,uint32_t rate,uint16_t channels,double start) -> std::shared_ptr<void> {
                    auto playback=audio->playPcm(std::move(pcm),rate,channels,start);
                    return playback?std::shared_ptr<void>(playback.release(),[](void* p){delete static_cast<d2::Audio::Playback*>(p);}):std::shared_ptr<void>();
                });
                if(audio)loadingFront->setSoundPlayer([&](const std::string& path){audio->effect(path);});
                if(audio) {
                    const std::string menuMusic="data\\global\\music\\common\\options.wav";
                    audio->preload(menuMusic);
                    loadingFront->setMusicPlayer([&](const std::string& path){audio->music(path);});
                    audio->music(menuMusic);
                }
                if(backFromGame)loadingFront->showMainMenu();
                // TCP/IP games (OTHER MULTIPLAYER), where the game runs in this program; the address joined last is
                // remembered in the save folder, as the original keeps it in the registry.
                const auto lastAddressFile=profile/L"LastTcpIp.txt";
                if(d2::lanAvailable()) {
                    std::string lastAddress;
                    if(std::ifstream in(lastAddressFile);in)std::getline(in,lastAddress);
                    loadingFront->enableLan(d2::lanLocalAddress,d2::lanValidAddress,lastAddress);
                    if(lanFailure){loadingFront->showLanMessage(*lanFailure);lanFailure.reset();}
                }
                frontEnd=loadingFront.get();
                window->show();
#ifdef __EMSCRIPTEN__
                // One frame of the menus per browser frame. Quitting tells the page; choosing a character starts run() again
                // with it (the difficulty goes to the game as on the desktop below; there are no TCP/IP games in a browser).
                webFront=std::move(loadingFront);
                gWebFrame=[] {
                    static auto last=std::chrono::steady_clock::now();
                    auto& front=*webFront;
                    window->setTextInput(front.wantsText());
                    touch::update();
                    bool closed=false;
                    for(d2::InputEvent e;window->pollEvent(e);){if(e.type==d2::InputEvent::Type::Quit)closed=true;else handleInput(e);}
                    const auto now=std::chrono::steady_clock::now();
                    front.advance(std::chrono::duration<double>(now-last).count());last=now;
                    if(audio)audio->update();
                    if(renderer.drawScreen(front.draw()))renderer.present();
                    if(closed||front.quit()) {
                        emscripten_cancel_main_loop();
                        frontEnd=nullptr;webFront.reset();
                        webGameEnded();
                        return;
                    }
                    if(front.screen()!=d2::FrontEnd::Screen::Loading||!front.chosen())return;
                    if(audio)audio->music("");
                    webChosen=front.chosen()->path;
                    d2::setEnvironment("D2NEXT_DIFFICULTY",std::to_string(front.difficulty()).c_str());
                    emscripten_cancel_main_loop();
                    webRun();
                };
                emscripten_set_main_loop(webFrameTrampoline,0,1);
                return 0;
#endif
                auto last=std::chrono::steady_clock::now();
                while(!loadingClosed&&!loadingFront->quit()&&loadingFront->screen()!=d2::FrontEnd::Screen::Loading) {
                    window->setTextInput(loadingFront->wantsText()); // a new character's name, or a host's address
                    touch::update(); // a game controller's cursor
                    for(d2::InputEvent e;window->pollEvent(e);){if(e.type==d2::InputEvent::Type::Quit)loadingClosed=true;else handleInput(e);}
                    const auto now=std::chrono::steady_clock::now();
                    loadingFront->advance(std::chrono::duration<double>(now-last).count());last=now;
                    if(audio)audio->update();
                    if(renderer.drawScreen(loadingFront->draw())){saveRequestedShot([&](const std::filesystem::path& p){renderer.readback(p);});renderer.present();}
                    yieldFrame(15);
                }
                if(!loadingClosed&&!loadingFront->quit()) {
                    if(audio)audio->music("");
                    // The loading screen stays up while the game starts.
                    loadingLast=std::chrono::steady_clock::now();
                    loadingDrawLast=loadingLast-std::chrono::milliseconds(33);
                    pumpLoading();
                    chosen=loadingFront->chosen()->path;
                    d2::setEnvironment("D2NEXT_DIFFICULTY",std::to_string(loadingFront->difficulty()).c_str()); // read as the game is created
                    // How it plays: alone, hosting on the TCP/IP port, or joining the address typed (kept for next time).
                    const auto lan=loadingFront->lan();
                    d2::setEnvironment("D2MODERN_LAN_HOST",lan==d2::FrontEnd::Lan::Host?"4000":"");
                    d2::setEnvironment("D2MODERN_LAN_JOIN",lan==d2::FrontEnd::Lan::Join?loadingFront->lanAddress().c_str():"");
                    if(lan==d2::FrontEnd::Lan::Join) {
                        std::error_code error;std::filesystem::create_directories(profile,error);
                        std::ofstream(lastAddressFile)<<loadingFront->lanAddress()<<"\n";
                    }
                    menuLan=lan!=d2::FrontEnd::Lan::None;
                }
            }
            if(chosen.empty())return 0;
            nativeSave=chosen;lastPlayed=chosen;
            std::cout<<"Chosen character: "<<nativeSave.string()<<"\n";
        }
        WEB_STATIC d2::Bytes bytes;bytes=nativeDlls.empty()?archives.read(asset):d2::Bytes{};
        std::string extension=std::filesystem::path(asset).extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        WEB_STATIC bool tiles=false,map=false;
        tiles=extension==".dt1";map=extension==".ds1";
        const auto entries=map?1:tiles?d2::dt1TileCount(bytes):d2::dc6FrameCount(bytes);
        WEB_STATIC size_t count=0;count=tiles&&!singleTile?(entries+d2::tilesPerPage-1)/d2::tilesPerPage:entries;
        WEB_STATIC d2::MapScene scene;
        WEB_STATIC d2::CollisionGrid collision;
        d2::GridPoint spawn;
        // WEB_STATIC: Renderer keeps pointers into these. emscripten_set_main_loop reuses the C stack.
        WEB_STATIC d2::Animation idleCharacter,walkingCharacter;
        WEB_STATIC std::map<uint8_t,d2::Animation> movementCharacters;
        WEB_STATIC d2::Palette characterPalette{};
        WEB_STATIC std::filesystem::path lookFile; // where the player's look is remembered for character select
        // The player's animations for a look (class, weapon class, gear): town neutral and walk, and with the native game every
        // other mode the class has art for, in the game's order: death, neutral, walk, run, get hit, town neutral, town walk,
        // attack 1 and 2, block, cast, throw, kick, specials 1-4, dead, sequence, knockback. Death and dead exist only unarmed (hth).
        WEB_STATIC std::function<void(const d2::CharacterAppearance&)> loadLook;
        loadLook=[&](const d2::CharacterAppearance& look) {
            auto set=buildLook(archives,characterPalette,look,!nativeSave.empty());
            idleCharacter=std::move(set.idle);walkingCharacter=std::move(set.walking);movementCharacters=std::move(set.movement);
        };
        WEB_STATIC std::unique_ptr<d2::UnitArt> unitArt;
        WEB_STATIC std::unique_ptr<d2::StringTable> strings;
        WEB_STATIC std::unique_ptr<d2::ItemTable> itemTable;WEB_STATIC std::unique_ptr<d2::ItemStatTable> itemStats;WEB_STATIC std::unique_ptr<d2::ItemArt> itemArt;WEB_STATIC std::unique_ptr<d2::SkillTable> skillTable;WEB_STATIC std::unique_ptr<d2::SkillArt> skillArt;WEB_STATIC std::unique_ptr<d2::ObjectTable> objectTable;WEB_STATIC std::unique_ptr<d2::ObjectArt> objectArt;WEB_STATIC std::unique_ptr<d2::LevelTable> levelTable;WEB_STATIC std::unique_ptr<d2::PetTypeTable> petTypes;WEB_STATIC std::unique_ptr<d2::AutomapTable> automapTable;WEB_STATIC std::unique_ptr<d2::WarpTable> warpTable;WEB_STATIC std::unique_ptr<d2::MissileTable> missileTable;WEB_STATIC std::unique_ptr<d2::MissileArt> missileArt;WEB_STATIC std::unique_ptr<d2::OverlayTable> overlayTable;WEB_STATIC std::unique_ptr<d2::StateTable> stateTable;WEB_STATIC std::unique_ptr<d2::StateColors> stateColors;WEB_STATIC std::unique_ptr<d2::OverlayArt> overlayArt;
        if(map) {
            const auto layout=nativeDlls.empty()?d2::decodeDs1(bytes):d2::MapLayout{};
            if(nativeDlls.empty()) {
                scene=d2::buildMapScene(layout,archives);
                std::cout<<"DS1 v"<<layout.version<<" | "<<layout.width<<"x"<<layout.height<<" cells | "<<scene.dependencies<<" tile files | "<<scene.placements.size()<<" placed | "<<scene.missing<<" unresolved | "<<layout.objects<<" object records (not drawn)\n";
            } else {
                if(!nativeSave.empty()&&!seedGiven)nativeSeed=d2::characterMapSeed(nativeSave,profile,smoke,nativeSeed);
                auto town=d2::loadNativeTown(nativeDlls,root,archives,nativeSeed,nativeSave.empty()?std::vector<int>{1}:std::vector<int>{1,2,3,4},!nativeSave.empty(),pumpLoading);
                activeAct=town.act+1;
                std::cout<<"Native "<<actName(activeAct)<<" town | seed "<<town.seed<<" | "<<town.rooms<<" rooms | world subtile origin "<<town.worldOrigin.x<<","<<town.worldOrigin.y<<" | "<<town.scene.placements.size()<<" tiles | "<<(nativeSave.empty()?"prototype movement":"D2Game movement")<<"\n";
                scene=std::move(town.scene);collision=std::move(town.collision);
                nativeOrigin=town.worldOrigin;sceneComplete=town.complete;sceneRooms=town.roomAreas;sceneOrigin=town.worldOrigin;
                asset="Native "+actName(activeAct)+" town (seed "+std::to_string(nativeSeed)+")";
            }
            if(verify&&scene.missing)throw std::runtime_error("Map verification failed: unresolved tile references");
            if(mapStats) {
                // Diagnostic: placements per tile orientation, with image height range.
                struct Stat {size_t count=0;uint32_t minHeight=~0u,maxHeight=0;};
                std::map<std::pair<bool,uint32_t>,Stat> stats;
                for(const auto& p:scene.placements) {
                    const auto& t=scene.tiles[p.tile];auto& s=stats[{p.shadow,t.orientation}];
                    ++s.count;s.minHeight=std::min(s.minHeight,t.image.height);s.maxHeight=std::max(s.maxHeight,t.image.height);
                }
                for(const auto& [key,s]:stats)std::cout<<(key.first?"shadow ":"tile   ")<<"orientation "<<key.second<<": "<<s.count<<" placements, image height "<<s.minHeight<<".."<<s.maxHeight<<"\n";
                // Solid footprint of each unique tile: bounding box of subtiles flagged wall/no-player
                // (DT1 rows are stored bottom to top), in subtile units within the cell.
                std::map<std::string,size_t> boxes;
                for(const auto& p:scene.placements) {
                    if(p.shadow)continue;
                    const auto& t=scene.tiles[p.tile];int x0=9,y0=9,x1=-1,y1=-1;
                    for(int y=0;y<5;++y)for(int x=0;x<5;++x)if(t.collision[(4-y)*5+x]&0x09){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
                    std::string label=x1<0?"no solid subtiles":"x "+std::to_string(x0)+".."+std::to_string(x1)+" y "+std::to_string(y0)+".."+std::to_string(y1);
                    ++boxes["orientation "+std::to_string(t.orientation)+" : "+label];
                }
                std::cout<<"-- solid footprint (subtile x/y range within the cell) --\n";
                for(const auto& [label,n]:boxes)if(!label.starts_with("orientation 0 "))std::cout<<label<<" : "<<n<<"\n";
                return 0;
            }
            if(nativeDlls.empty())collision=d2::buildCollision(scene);
            size_t passable=0;for(int y=0;y<int(collision.height);++y)for(int x=0;x<int(collision.width);++x)passable+=collision.walkable({x,y});
            std::cout<<"Collision: "<<collision.width<<"x"<<collision.height<<" subtiles | "<<passable<<" walkable\n";
            if(walk&&!referenceMap){
                spawn=d2::findSpawn(collision);
                if(havePlace) {
                    if(!collision.walkable(placeAt))throw std::runtime_error("--place subtile is not walkable");
                    spawn=placeAt;
                }
                if(nativeSave.empty())world=d2::makePrototypeBackend(collision,spawn);
                else {
                    // Progress lives in the local profile folder. An explicitly imported save is copied here on its first save.
                    std::filesystem::path saveSource=nativeSave,saveOut;
                    if(!smoke||(std::getenv("D2MODERN_TEST_SAVE")&&!profileDir.empty())) { // a test saves only into a profile folder it names
                        std::error_code profileError;std::filesystem::create_directories(profile,profileError);
                        saveOut=profile/nativeSave.filename();
                        if(std::filesystem::exists(saveOut))saveSource=saveOut;
                        std::cout<<"Profile: "<<saveOut.string()<<(saveSource==saveOut?" (continuing)":" (new)")<<"\n";
                    }
                    if(allWaypoints)d2::setEnvironment("D2MODERN_TEST_WAYPOINTS","1"); // every Normal waypoint is known (--no-waypoints for a normal start)
                    lastPlayed=nativeSave;
                    if(smoke)d2::setEnvironment("D2MODERN_LAN_WAIT","1"); // a scripted LAN guest waits for each of the host's ticks
                    d2::NativeGameStart session;
                    {   // What the death screen needs: hardcore (the save's status byte 0x24, bit 4) and the difficulty chosen.
                        characterHardcore=false;std::ifstream save(saveSource,std::ios::binary);char status=0;
                        if(save.seekg(0x24)&&save.read(&status,1)){characterHardcore=(status&0x04)!=0;expansionGame=(status&0x20)!=0;}
                        const char* level=std::getenv("D2NEXT_DIFFICULTY");gameDifficulty=level?std::clamp(std::atoi(level),0,2):0;
                    }
                    try {session=d2::startNativeGame(nativeDlls,root,saveSource,nativeSeed,collision,nativeOrigin,std::vector<int>{1,2,3,4},saveOut,pumpLoading);}
                    catch(const std::exception& e) {
                        // Preserve the socket or handshake reason for both host and guest: the generic game message
                        // otherwise hides whether the address is unreachable or the host rejected the save.
                        if(!menuLan)throw;
                        std::cout<<"TCP/IP game failed: "<<e.what()<<std::endl;
                        lanFailure=std::string(e.what());
                        leaveToMainMenu=true;frontEnd=nullptr;
                        return 0;
                    }
                    pumpLoading();
                    appearance=session.appearance;characterClass=int32_t(session.characterClass);
                    for(const auto& c:d2::findCharacters({saveSource.parent_path()}))if(c.path.filename()==saveSource.filename())characterName=c.name;
                    // Character select shows the character in this gear next time.
                    if(!saveOut.empty()){lookFile=saveOut.parent_path()/(saveOut.stem().wstring()+L".look");d2::saveAppearance(lookFile,appearance);}
                    std::cout<<"Native D2Game player | class "<<session.characterClass<<" | read-only save | "<<appearance.token<<" / "<<appearance.weaponClass<<"\n";
                    world=std::move(session.backend);
                    if(session.seed!=nativeSeed) {
                        // A LAN guest plays in the host's game: its maps come from the host's seed, so the scene already
                        // loaded (from this character's own seed) is built again below.
                        std::cout<<"Map seed "<<session.seed<<" (the host's)\n";
                        nativeSeed=session.seed;sceneComplete.clear();
                    }
                    if(const auto startLevel=int(world->snapshot().level);startLevel&&std::find(sceneComplete.begin(),sceneComplete.end(),startLevel)==sceneComplete.end()) {
                        std::cout<<"Loading saved level "<<startLevel<<"..."<<std::endl;
                        auto startScene=d2::loadNativeTown(nativeDlls,root,archives,nativeSeed,{startLevel},true,pumpLoading);
                        activeAct=startScene.act+1;
                        scene=std::move(startScene.scene);collision=std::move(startScene.collision);nativeOrigin=startScene.worldOrigin;sceneComplete=startScene.complete;sceneRooms=startScene.roomAreas;sceneOrigin=startScene.worldOrigin;
                        asset="Native "+actName(activeAct)+" town (seed "+std::to_string(nativeSeed)+")";
                        world->setScene(collision,nativeOrigin,startScene.levels);
                        world->tick();
                    }
                    spawn=d2::subtileOf(world->snapshot().player().position);
                }
                worldView=world->snapshot();
                walkGrid=&collision;walkScene=&scene;followPlayer=true;
            }
            if(walkGrid&&!markerOnly) {
                characterPalette=loadActPalette();
                pumpLoading();
                loadLook(appearance);
                pumpLoading();
                std::cout<<appearance.token<<": "<<idleCharacter.directions<<" directions | "<<idleCharacter.framesPerDirection<<" idle frames | "<<walkingCharacter.framesPerDirection<<" walk frames\n";
            }
            if(walkGrid&&!nativeSave.empty()){unitArt=std::make_unique<d2::UnitArt>(archives,loadActPalette());pumpLoading();}
            if(walkGrid&&!nativeSave.empty()){const auto scenePalette=loadActPalette();strings=std::make_unique<d2::StringTable>(archives);gameStrings=strings.get();pumpLoading();itemTable=std::make_unique<d2::ItemTable>(archives,*strings);gameItems=itemTable.get();itemStats=std::make_unique<d2::ItemStatTable>(archives);itemArt=std::make_unique<d2::ItemArt>(archives,palette,*itemTable);pumpLoading();skillTable=std::make_unique<d2::SkillTable>(archives,*strings);gameSkills=skillTable.get();skillArt=std::make_unique<d2::SkillArt>(archives,palette);pumpLoading();levelTable=std::make_unique<d2::LevelTable>(archives);gameLevels=levelTable.get();petTypes=std::make_unique<d2::PetTypeTable>(archives);gamePetTypes=petTypes.get();automapTable=std::make_unique<d2::AutomapTable>(archives);warpTable=std::make_unique<d2::WarpTable>(archives);missileTable=std::make_unique<d2::MissileTable>(archives);gameMissiles=missileTable.get();missileArt=std::make_unique<d2::MissileArt>(archives,scenePalette,*missileTable);pumpLoading();overlayTable=std::make_unique<d2::OverlayTable>(archives);gameOverlays=overlayTable.get();stateTable=std::make_unique<d2::StateTable>(archives);overlayArt=std::make_unique<d2::OverlayArt>(archives,scenePalette,*overlayTable);objectTable=std::make_unique<d2::ObjectTable>(archives);objectArt=std::make_unique<d2::ObjectArt>(archives,scenePalette,*objectTable);pumpLoading();gameStates=stateTable.get();stateColors=std::make_unique<d2::StateColors>(archives,activeAct);if(unitArt)unitArt->setStateColors(stateColors.get());gameUnitArt=unitArt.get();}
        }
        frontEnd=nullptr;
        loadingFront.reset();
        if(loadingClosed)return 0;
        if(!nativeSave.empty())renderer.forgetImages();
        WEB_STATIC std::function<d2::Image(uint32_t)> decode;
        decode=[&](uint32_t index) {
            if(map) {
                return referenceMap?d2::composeMap(scene):d2::Image{scene.width,scene.height,{}};
            }
            if(!tiles) return d2::decodeDc6(bytes,palette,index);
            return singleTile?d2::decodeDt1(bytes,palette,index).image:d2::dt1Atlas(bytes,palette,index);
        };
        if(verify) {
            for(uint32_t f=0;f<entries;++f) {
                if(map)decode(0); else if(tiles) d2::decodeDt1(bytes,palette,f); else d2::decodeDc6(bytes,palette,f);
            }
            std::cout<<"Verified "<<entries<<(map?" DS1 layout":tiles?" DT1 tiles":" DC6 frames")<<" | "<<asset<<"\n";
            return 0;
        }
        auto image=decode(0);
        if(tiles||map) initialZoom=zoom=std::min({800.0f/image.width,500.0f/image.height,1.0f});
        if(requestedZoom)initialZoom=zoom=requestedZoom;
        else if(walkGrid)initialZoom=zoom=touch::viewZoomScale();
        std::cout<<"x64 | "<<archives.count()<<" archives | "<<asset<<" | "<<image.width<<"x"<<image.height<<" | "<<entries<<(map?" map\n":tiles?" tiles\n":" frames\n");
        renderer.unitArt=unitArt.get();renderer.strings=strings.get();renderer.itemTable=itemTable.get();renderer.itemStats=itemStats.get();renderer.itemArt=itemArt.get();renderer.skillTable=skillTable.get();renderer.skillArt=skillArt.get();renderer.objectTable=objectTable.get();renderer.objectArt=objectArt.get();renderer.levelTable=levelTable.get();renderer.automapTable=automapTable.get();renderer.missileTable=missileTable.get();renderer.warpTable=warpTable.get();renderer.missileArt=missileArt.get();renderer.overlayTable=overlayTable.get();renderer.stateTable=stateTable.get();renderer.stateColors=stateColors.get();renderer.unitPalette=&characterPalette;renderer.archives=&archives;renderer.playerLook=&appearance;renderer.overlayArt=overlayArt.get();renderer.uiAssets=&uiAssets;
        try {
            auto bytes=archives.read("data\\global\\excel\\experience.txt");
            const d2::TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
            for(size_t r=0;r<table.rows();++r){const auto level=table.cell(r,"Level");if(!level.empty()&&std::isdigit(static_cast<unsigned char>(level[0])))renderer.levelExperience.push_back(std::stoll(table.cell(r,"Amazon")));}
        } catch(const std::exception&) {}
        WEB_STATIC std::unique_ptr<d2::Font> font42,font30,font16,font8,font6;WEB_STATIC std::unique_ptr<d2::TextCache> text42,text30,text16,text8,text6;
        try {
            const auto units=d2::decodePalette(archives.read("data\\global\\palette\\units\\pal.dat"));
            font42=std::make_unique<d2::Font>(archives,units,"font42");font16=std::make_unique<d2::Font>(archives,units,"font16");font8=std::make_unique<d2::Font>(archives,units,"font8");
            text42=std::make_unique<d2::TextCache>(*font42);text16=std::make_unique<d2::TextCache>(*font16);text8=std::make_unique<d2::TextCache>(*font8);
            font6=std::make_unique<d2::Font>(archives,units,"font6");text6=std::make_unique<d2::TextCache>(*font6);
            font30=std::make_unique<d2::Font>(archives,units,"font30");text30=std::make_unique<d2::TextCache>(*font30);renderer.text30=text30.get();
            renderer.text42=text42.get();renderer.text16=text16.get();renderer.text8=text8.get();renderer.text6=text6.get();
        } catch(const std::exception&) {}
        // Walking into a level outside the loaded scene (a dungeon, the next wilderness area): generate that level's scene with the same seed and swap it in.
        auto& loadedLevels=sceneComplete;
        WEB_STATIC std::array<uint32_t,17> shownLook;shownLook=worldView.appearance;
        if(walkGrid&&!nativeSave.empty())afterTick=[&] {
            {const auto& p=worldView.player().position;renderer.revealAround(float(p.x)/d2::fixedOne,float(p.y)/d2::fixedOne);}
            // New gear (a weapon of another class, armour, a helm, a shield): the player's animations are rebuilt for it.
            {
                // New gear: the looks worn last are kept, so changing back is at once; a new one is built on another thread
                // while the old one is still drawn, and shown when it is ready (the web build, single-threaded, builds it here).
                // Moving animations keeps their pictures where they are, so their textures stay valid; a look dropped from the
                // cache frees its pictures, so the textures are forgotten then (and when new pictures arrive).
                static std::list<std::pair<std::array<uint32_t,17>,LookSet>> lookCache;
                static std::array<uint32_t,17> requested{};
                static auto requestedAt=std::chrono::steady_clock::now();
                auto show=[&](const std::array<uint32_t,17>& key,const d2::CharacterAppearance& look,LookSet&& set,bool cached) {
                    lookCache.push_front({shownLook,LookSet{std::move(idleCharacter),std::move(walkingCharacter),std::move(movementCharacters)}});
                    idleCharacter=std::move(set.idle);walkingCharacter=std::move(set.walking);movementCharacters=std::move(set.movement);
                    shownLook=key;appearance=look;
                    bool evicted=false;while(lookCache.size()>2){lookCache.pop_back();evicted=true;}
                    renderer.clearMovement();
                    if(evicted||!cached)renderer.forgetImages();
                    renderer.uploadCharacters(idleCharacter,walkingCharacter);
                    for(const auto& [mode,animation]:movementCharacters)renderer.uploadMovement(mode,animation);
                    if(!lookFile.empty())d2::saveAppearance(lookFile,appearance);
                    std::cout<<"New look: "<<appearance.token<<" / "<<appearance.weaponClass<<", "<<movementCharacters.size()<<" animation modes"<<(cached?" (kept from before)":"")
                        <<", "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-requestedAt).count()<<" ms"<<std::endl;
                };
#ifndef __EMSCRIPTEN__
                static std::unique_ptr<LookBuilder> builder;
                if(!builder&&!markerOnly&&!nativeSave.empty()&&!std::getenv("D2MODERN_LOOK_SYNC"))builder=std::make_unique<LookBuilder>(archives.root(),characterPalette,true); // ready before it is needed
                if(builder)if(auto done=builder->take()) {
                    if(done->failed)std::cout<<"Cannot show the new gear: "<<done->error<<std::endl;
                    else if(done->key==worldView.appearance&&done->key!=shownLook)show(done->key,done->look,std::move(done->set),false);
                    else if(done->key!=shownLook)lookCache.push_back({done->key,std::move(done->set)}); // already changed again: kept for later
                    while(lookCache.size()>2)lookCache.pop_back();
                }
#endif
                if(!markerOnly&&worldView.appearance[0]&&worldView.appearance!=shownLook&&worldView.appearance!=requested) {
                    requested=worldView.appearance;requestedAt=std::chrono::steady_clock::now();
                    try {
                        const auto look=d2::appearanceFromCodes(uint32_t(characterClass),worldView.appearance);
                        if(auto hit=std::find_if(lookCache.begin(),lookCache.end(),[&](const auto& entry){return entry.first==worldView.appearance;});hit!=lookCache.end()) {
                            LookSet set=std::move(hit->second);lookCache.erase(hit);
                            show(worldView.appearance,look,std::move(set),true);
                        } else {
#ifndef __EMSCRIPTEN__
                            if(std::getenv("D2MODERN_LOOK_SYNC"))show(worldView.appearance,look,buildLook(archives,characterPalette,look,!nativeSave.empty()),false); // test hook: built here
                            else {
                                if(!builder)builder=std::make_unique<LookBuilder>(archives.root(),characterPalette,!nativeSave.empty());
                                builder->request(worldView.appearance,look);
                            }
#else
                            show(worldView.appearance,look,buildLook(archives,characterPalette,look,!nativeSave.empty()),false);
#endif
                        }
                    } catch(const std::exception& e) {std::cout<<"Cannot show the new gear: "<<e.what()<<std::endl;}
                }
                if(worldView.appearance==shownLook)requested=shownLook;
            }
            static uint64_t retryAt=0;
            if(!worldView.level||std::find(loadedLevels.begin(),loadedLevels.end(),int(worldView.level))!=loadedLevels.end()||worldView.tick<retryAt)return;
            std::cout<<"Loading level "<<worldView.level<<"..."<<std::endl;const auto loadStart=std::chrono::steady_clock::now();
            try {
                auto next=d2::loadNativeTown(nativeDlls,root,archives,nativeSeed,{int(worldView.level)},true);
                scene=std::move(next.scene);collision=std::move(next.collision);nativeOrigin=next.worldOrigin;loadedLevels=next.complete;sceneRooms=next.roomAreas;sceneOrigin=next.worldOrigin;
                world->setScene(collision,nativeOrigin,next.levels);
                renderer.upload(scene);
                std::cout<<"Level "<<worldView.level<<": "<<scene.placements.size()<<" tiles, "<<next.levels.size()<<" level(s) in the scene, loaded in "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-loadStart).count()<<" ms"<<std::endl;
            } catch(const std::exception& e) {
                std::cout<<"Cannot load level "<<worldView.level<<": "<<e.what()<<" (will retry)"<<std::endl;retryAt=worldView.tick+50;walkStatus="Cannot load this area";
            }
        };
        WEB_STATIC std::unique_ptr<d2::MonsterTable> soundMonsters;WEB_STATIC std::unique_ptr<d2::SoundBank> soundBank;
        WEB_STATIC std::function<void()> updateAudio;
        updateAudio=[&] {
            const auto& p=worldView.player();
            static uint8_t lastMode=255;static uint32_t lastActionFrame=0;static int lastLevel=-1;
            const uint32_t actionFrame=p.animationFrame==UINT32_MAX?0:p.animationFrame;
            const bool actionRestarted=p.mode==lastMode&&p.animationFrame!=UINT32_MAX&&actionFrame<lastActionFrame;
            const bool actionMode=(p.mode>=7&&p.mode<=8)||p.mode==10||(p.mode>=13&&p.mode<=16)||p.mode==18;
            const bool actionStarted=actionMode&&(p.mode!=lastMode||actionRestarted);
            // The level's sounds, from its soundenviron.txt row (levels.txt SoundEnv), as D2Client 0x6FB53860 plays them each frame.
            const auto* levelInfo=levelTable?levelTable->byId(int32_t(worldView.level)):nullptr;
            if(worldView.level!=lastLevel&&(soundBank||!levelInfo)) {
                lastLevel=worldView.level;
                // Its music: Song, a sounds.txt row naming a track under data\global\music.
                if(levelInfo) {
                    if(const auto track=soundBank->fileByIndex(levelInfo->sounds.song);!track.empty())audio->music("data\\global\\music\\"+track);
                } else {
                    // Without the game's tables (the stand-in backends, Act I only): town, wilderness, Tristram, monastery, crypts.
                    const auto level=worldView.level;
                    const char* track=level==1?"town1":level<=7||level==39?"wild":level==38?"tristram":level>=26?"monastery":"crypt";
                    audio->music(std::string("data\\global\\music\\Act1\\")+track+".wav");
                }
            }
            if(levelInfo&&soundBank) {
                // Day is cycles 1-3 (morning to afternoon, D2Common #10930); sunrise, sunset and night take the night sounds.
                const size_t night=worldView.envCycle>=1&&worldView.envCycle<=3?0:1;
                // The ambience: a looping scene sound, at its sounds.txt volume. Changing it ends the one before.
                static int ambience=-1;
                const int nextAmbience=levelInfo->sounds.ambience[night];
                if(nextAmbience!=ambience) {
                    audio->stopLoop("ambience");ambience=nextAmbience;
                    if(const auto file=soundBank->fileByIndex(ambience);!file.empty())audio->startLoop("ambience",file,soundBank->volumeByIndex(ambience)/255.f);
                }
                // Events (birds, drips, moans): one of the event's sounds.txt group every Event Delay frames plus up to a third
                // more at random; the first comes after a random part of that, so entering an area does not start with one.
                static int event=-1;static uint32_t eventLast=0,eventWait=0;
                static std::mt19937 eventRandom{std::random_device{}()};
                auto randomBelow=[&](uint32_t n){return n?uint32_t(eventRandom()%n):0u;};
                const int nextEvent=levelInfo->sounds.event[night];const uint32_t delay=uint32_t(std::max(0,levelInfo->sounds.eventDelay));
                if(nextEvent!=event) {
                    event=nextEvent;eventWait=delay+randomBelow(delay/3);
                    eventLast=worldView.tick-randomBelow(eventWait);
                } else if(event>0&&worldView.tick-eventLast>=eventWait) {
                    const int group=soundBank->groupByIndex(event);
                    const int row=event+int(randomBelow(uint32_t(std::max(group,1))));
                    if(const auto file=soundBank->fileByIndex(row);!file.empty())audio->effect(file,soundBank->volumeByIndex(row)/255.f);
                    eventLast=worldView.tick;eventWait=delay+randomBelow(delay/3);
                }
            }
            if(soundBank) {
                // The rain (not the snow): sounds.txt scene_rain, louder the heavier the storm (D2Client 0x6FB539E7: the
                // storm's strength times 255, moving at most 6 a frame toward it).
                static int rainVolume=0;
                const int target=gameWeather.snowing()?0:std::clamp(int(gameWeather.strength()*255.f),0,255);
                if(target!=rainVolume)rainVolume=target>rainVolume?std::min(target,rainVolume+6):std::max(target,rainVolume-6);
                if(rainVolume>0) {
                    if(const auto file=soundBank->file("scene_rain");!file.empty())audio->startLoop("rain",file,rainVolume/255.f);
                    audio->loopGain("rain",rainVolume/255.f);
                } else audio->stopLoop("rain");
            }
            lastMode=p.mode;lastActionFrame=actionFrame;
            // Sounds of what happens elsewhere (another player's fight in a TCP/IP game, a monster across the area) are
            // heard only near this player: within 30 subtiles, the radius missile hits already used.
            auto nearPlayer=[&](d2::FixedPoint at){const double dx=double(at.x-p.position.x)/d2::fixedOne,dy=double(at.y-p.position.y)/d2::fixedOne;return dx*dx+dy*dy<30.0*30.0;};
            // Monsters getting hit, knocked back or dying, near the player, with their own sounds.
            static std::map<int64_t,uint8_t> monsterModes;
            std::map<int64_t,uint8_t> monsterModesNow;
            auto playImpact=[&](uint8_t hitClass){
                if(!soundBank)return;
                const uint8_t physical=hitClass&0x0F;
                if(!physical)return;
                const char* name=nullptr;
                if(physical==1||physical==8||physical==9)name="impact_bash";
                else {
                    static unsigned stab=0;
                    static const char* stabs[]={"impact_blade_thrust_1","impact_blade_thrust_2","impact_blade_thrust_3","impact_blade_thrust_4","impact_blade_thrust_5"};
                    name=stabs[stab++%5];
                }
                if(const auto file=soundBank->file(name);!file.empty())audio->effect(file);
            };
            for(const auto& e:worldView.entities) {
                if(e.kind!=d2::EntityKind::Enemy&&e.kind!=d2::EntityKind::Corpse)continue;
                auto found=monsterModes.find(e.id);const uint8_t before=found==monsterModes.end()?uint8_t(255):found->second;
                if(before!=255&&before!=e.mode&&soundBank&&nearPlayer(e.position)) {
                    if(e.mode==3||e.mode==13||e.mode==0) {
                        const auto file=e.mode==0?soundBank->monsterDeath(e.classId):soundBank->monsterHit(e.classId);
                        if(!file.empty())audio->effect(file);
                        if(e.mode!=0)playImpact(e.hitClass);
                    } else if(e.mode==4||e.mode==5) {
                        if(const auto file=soundBank->monsterAttack(e.classId,e.mode);!file.empty())audio->effect(file);
                        if(const auto file=soundBank->monsterWeapon(e.classId,e.mode);!file.empty())audio->effect(file);
                    } else if(e.mode==6) {
                        static unsigned block=0;
                        if(const auto file=soundBank->file(std::string("block_weapon_")+std::to_string(1+block++%3));!file.empty())audio->effect(file);
                    }
                }
                monsterModesNow[e.id]=e.mode;
            }
            monsterModes=std::move(monsterModesNow);
            // The player's items: picked up onto the cursor (cursor_pickup), put away or worn (the item's usesound), dropped on the
            // ground (its dropsound), a potion drunk from the belt or inventory (item_potion_drink). Gold picked up (item_gold)
            // and a new character level (cursor_level_up).
            static std::map<uint32_t,std::pair<uint32_t,uint32_t>> itemsBefore; // id -> mode, code
            static bool itemsKnown=false;static int32_t goldBefore=-1,levelBefore=-1;
            static uint32_t itemsLevel=0;if(worldView.level!=itemsLevel){itemsLevel=worldView.level;itemsKnown=false;} // a new area's items are not drops
            std::map<uint32_t,std::pair<uint32_t,uint32_t>> itemsNow;
            for(const auto& item:worldView.items)if(!item.vendor)itemsNow[item.id]={item.mode,item.code};
            const bool trading=npcDialog.open;
            if(itemsKnown&&itemTable&&soundBank) {
                auto play=[&](const std::string& name){if(const auto f=soundBank->file(name);!f.empty())audio->effect(f);};
                for(const auto& [id,now]:itemsNow) {
                    const auto before=itemsBefore.find(id);
                    if(before==itemsBefore.end()) {
                        // A new item on the ground near the player: dropped by a monster (the game reports it already landed).
                        if(now.first==3||now.first==5)for(const auto& item:worldView.items)if(item.id==id) {
                            const double dx=item.x-double(p.position.x)/d2::fixedOne,dy=item.y-double(p.position.y)/d2::fixedOne;
                            if(dx*dx+dy*dy<15.0*15.0)if(const auto* info=itemTable->find(now.second))play(info->dropSound);
                        }
                        continue;
                    }
                    if(before->second.first==now.first)continue;
                    const auto* info=itemTable->find(now.second);
                    if(now.first==4)play("item_pickup");
                    // An item landing on the ground after it tumbled down (mode 5, dropped by a monster or by the player).
                    else if(before->second.first==5&&now.first==3&&info)play(info->dropSound);
                    else if(before->second.first==4&&now.first==5){} // leaving the cursor to tumble down: the sound comes as it lands
                    else if(before->second.first==4&&info) {
                        // A potion's usesound is drinking it; put away it makes its dropsound (item_potion, the potion handled).
                        const bool potion=info->type.size()==4&&info->type.compare(1,3,"pot")==0;
                        play(now.first==3||now.first==5||potion?info->dropSound:info->useSound);
                    }
                }
                for(const auto& [id,before]:itemsBefore)if(!itemsNow.count(id)&&(before.first==0||before.first==2)&&!trading)
                    if(const auto* info=itemTable->find(before.second);info&&info->type.size()==4&&info->type.compare(1,3,"pot")==0)play("item_potion_drink");
                if(goldBefore>=0&&worldView.stats[14]>goldBefore&&!trading)play("item_gold");
                if(levelBefore>0&&worldView.stats[12]>levelBefore)play("cursor_level_up");
            }
            itemsBefore=std::move(itemsNow);itemsKnown=true;goldBefore=worldView.stats[14];levelBefore=worldView.stats[12];
            if(soundBank) {
                auto play=[&](const std::string& name){if(const auto f=soundBank->file(name);!f.empty())audio->effect(f);};
                // Attacks, casts and special moves, as D2Client 1.10f times the skill's sounds: stsound when the action begins
                // (0x6FABC475; prgsound with it, which the original plays from a few skills' own functions), dosound and, with a
                // target, tgtsound on the frame event, after the do-function (0x6FABC94E). Jab's do-function (16, 0x6FABE760) adds
                // dosound a for a player on its sound frame (dosound b is a monster's).
                bool playedSkillAction=false;
                if(actionStarted&&skillTable)if(const auto* skill=skillTable->find(uint32_t(castSkill))) {
                    if(!skill->startSound.empty()){play(skill->startSound);playedSkillAction=true;}
                    const bool chargeUp=skill->clientProgress[0]==12||skill->clientProgress[0]==13; // their charge sound comes with packet 0xA3 (ClientState)
                    const auto* state=stateTable?stateTable->find(uint32_t(std::max(stateTable->rowOf(skill->auraState),0))):nullptr;
                    const bool stateLoop=state&&!skill->auraState.empty()&&state->clientActive==90; // looped while the state lasts (below)
                    if(!skill->progressSound.empty()&&!chargeUp&&!stateLoop){play(skill->progressSound);playedSkillAction=true;}
                    if(!skill->doSounds[0].empty())playedSkillAction=true; // its own sound comes on the frame event: no plain swing
                }
                if(pendingDoSoundSkill&&skillTable)if(const auto* skill=skillTable->find(pendingDoSoundSkill)) {
                    if(!skill->doSounds[0].empty()){play(skill->doSounds[0]);playedSkillAction=true;}
                    if(skill->clientDo==16&&!skill->doSounds[1].empty()){play(skill->doSounds[1]);playedSkillAction=true;}
                    if(actionTargetUnit&&!actionTargetIsPoint&&!skill->targetSound.empty())play(skill->targetSound);
                }
                pendingDoSoundSkill=0;
                if(pendingImpossibleSound&&characterClass>=0&&characterClass<7) {
                    static const char* heroes[]={"amazon","sorceress","necromancer","paladin","barbarian","druid","assassin"};
                    static unsigned line=0;play(std::string(heroes[characterClass])+"_impossible_"+std::to_string(1+line++%2));
                }
                pendingImpossibleSound=false;
                // A state whose client function is 90 (states.txt cltactivefunc, D2Client 0x6FAC2C90) keeps its skill's prgsound
                // going while it lasts: Hurricane's wind.
                if(stateTable&&skillTable)for(uint32_t s=0;s<192;++s)if(const auto* st=stateTable->find(s);st&&st->clientActive==90) {
                    const std::string key="state"+std::to_string(s);
                    if(p.states[s>>5]&(1u<<(s&31))) {
                        if(const auto* skill=st->skill.empty()?skillTable->withAuraState(st->name):skillTable->findByName(st->skill);skill&&!skill->progressSound.empty())
                            if(const auto file=soundBank->file(skill->progressSound);!file.empty())audio->startLoop(key,file);
                    } else audio->stopLoop(key);
                }
                if(actionStarted&&(p.mode==7||p.mode==8)&&!playedSkillAction) {
                    static const char* swings[]={"combat\\weapon\\one hand swing small01.wav","combat\\weapon\\one hand swing small03.wav","combat\\weapon\\one hand swing small05.wav"};
                    static unsigned swing=0;audio->effect(swings[swing++%3]);
                }
                // The character's own cries: hit (get-hit mode 4, sounds.txt <class>_hit_1-5) and death (mode 0, <class>_death_1-3).
                static uint8_t heroModeBefore=255;static unsigned cry=0;
                if(p.mode!=heroModeBefore&&characterClass>=0&&characterClass<7&&(p.mode==4||(p.mode==0&&heroModeBefore!=255))) {
                    static const char* heroes[]={"amazon","sorceress","necromancer","paladin","barbarian","druid","assassin"};
                    const bool death=p.mode==0;
                    play(std::string(heroes[characterClass])+(death?"_death_":"_hit_")+std::to_string(1+cry++%(death?3:5)));
                }
                heroModeBefore=p.mode;
                // Missiles: their travel sound when they appear, their hit sound when one near the player ends (missiles.txt).
                static std::map<int64_t,std::pair<uint32_t,d2::FixedPoint>> missilesBefore;
                std::map<int64_t,std::pair<uint32_t,d2::FixedPoint>> missilesNow;
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Missile)missilesNow[e.id]={e.classId,e.position};
                if(missileTable) {
                    for(const auto& [id,m]:missilesNow)if(!missilesBefore.count(id)&&nearPlayer(m.second))if(const auto* info=missileTable->find(m.first)) {
                        play(info->travelSound);
                        // A meteor's impact (ProgSound) is played 2 frames before the fall ends, not as the marker appears.
                        if(info->clientDo!=9)play(info->progressSound);
                    }
                    for(const auto& [id,m]:missilesBefore)if(!missilesNow.count(id))if(const auto* info=missileTable->find(m.first)) {
                        if(info->straightFall())continue; // the impact plays when the falling picture lands
                        if(nearPlayer(m.second))play(info->hitSound);
                    }
                    // pCltDoFunc 9: ProgSound at CltParam1-2 frames after the marker appears (the rock is about to land).
                    static std::map<int64_t,uint64_t> meteorImpact; // tick to play; 0 once it has played
                    for(const auto& [id,m]:missilesNow)if(const auto* info=missileTable->find(m.first);info&&info->clientDo==9&&info->clientParam[0]>2&&!info->progressSound.empty()) {
                        const auto when=meteorImpact.find(id);
                        if(when==meteorImpact.end())meteorImpact[id]=worldView.tick+uint64_t(info->clientParam[0]-2);
                        else if(when->second&&worldView.tick>=when->second){if(nearPlayer(missilesNow[id].second))play(info->progressSound);when->second=0;}
                    }
                    std::erase_if(meteorImpact,[&](const auto& e){return !missilesNow.count(e.first);});
                }
                missilesBefore=std::move(missilesNow);
                // Footsteps: two a walk or run cycle, by the character's weight (Amazon, Sorceress and Assassin light,
                // Necromancer and Druid medium, Barbarian and Paladin heavy) and the ground (stone inside, dirt outside).
                static int stepBefore=-1;static unsigned stepCount=0;
                if(p.moving&&(p.mode==2||p.mode==3||p.mode==6)) {
                    const auto found=movementCharacters.find(p.mode);
                    const unsigned frames=std::max(1u,found!=movementCharacters.end()?found->second.framesPerDirection:walkingCharacter.framesPerDirection);
                    const uint64_t frame=p.animationFrame==UINT32_MAX?p.stateTicks:p.animationFrame;
                    const int half=int((frame%frames)*2/frames);
                    if(stepBefore>=0&&half!=stepBefore) {
                        static const char* weights[]={"light","light","medium","heavy","heavy","medium","light"};
                        const auto* levelInfo=levelTable?levelTable->byId(int32_t(worldView.level)):nullptr;
                        const char* weight=characterClass>=0&&characterClass<7?weights[characterClass]:"light";
                        play(std::string(weight)+(p.mode==3?"_run_":"_walk_")+(levelInfo&&levelInfo->inside?"istone":"dirt")+"_"+std::to_string(1+stepCount++%4));
                    }
                    stepBefore=half;
                } else stepBefore=-1;
            }
            audio->update();
        };
        // Publishes the simulation state to the renderer, interpolated by alpha (0..1) between
        // the last two 25 Hz ticks, and keeps the camera on the character while following.
        WEB_STATIC std::function<void(float)> refreshView;
        refreshView=[&](float alpha) {
            if(!walkGrid)return;
            if(audio)updateAudio();
            const auto& p=worldView.player();const auto pos=d2::renderPosition(p,alpha);
            renderer.view={pos[0],pos[1],p.facing,p.moving,p.animationFrame==UINT32_MAX?p.stateTicks:p.animationFrame,p.animationFrame==UINT32_MAX?uint8_t(255):p.mode,p.skillMoveFrame,p.sequenceMode,p.sequenceFrame,p.shapeClass,p.shapeMode};
            if(std::getenv("D2MODERN_LEAP_LOG")&&(castSkill==d2::skillLeap||castSkill==d2::skillLeapAttack))
                std::cout<<"Leap visual: tick "<<worldView.tick<<", mode "<<int(renderer.view.mode)<<", S1 frame "<<int(renderer.view.skillMoveFrame)<<", moving "<<p.moving<<"\n";
            if(std::getenv("D2MODERN_SEQUENCE_LOG")&&p.mode==18)
                std::cout<<"Sequence: tick "<<worldView.tick<<", skill "<<castSkill<<", table mode "<<int(p.sequenceMode)<<" frame "<<int(p.sequenceFrame)<<", leap frame "<<int(p.skillMoveFrame)<<"\n";
            if(heldFacing>=0)renderer.view.facing=unsigned(heldFacing);
            // Casting: the spell's cast overlay (skills.txt castoverlay, overlay.txt) plays over the character from the cast's start
            // to its last frame, even when the cast animation itself is shorter.
            renderer.playerOverlay={};
            if(skillTable&&overlayTable&&overlayArt)if(const auto* skill=skillTable->find(uint32_t(castSkill));skill&&!skill->castOverlay.empty())
                if(const auto* info=overlayTable->find(skill->castOverlay)) {
                    const unsigned frame=unsigned((worldView.tick-castStart)*uint64_t(info->rate)/16);
                    if(frame<info->frames)renderer.playerOverlay={overlayArt->get(skill->castOverlay),frame,info->x,info->y,info->trans,float(info->radius),{info->color[0]/255.f,info->color[1]/255.f,info->color[2]/255.f},info->preDraw};
                }
            renderer.others.clear();
            for(size_t i=0;i<worldView.entities.size();++i) {
                const auto& e=worldView.entities[i];
                if(e.id==worldView.localPlayer)continue; // another player (LAN) is drawn like the rest
                // A falling shard is drawn as the client effect. The server missile is only the spot it is falling toward.
                if(e.kind==d2::EntityKind::Missile&&missileTable)if(const auto* info=missileTable->find(e.classId);info&&info->straightFall())continue;
                const auto q=d2::renderPosition(e,alpha);
                {static const char* forced=std::getenv("D2MODERN_TEST_TRANSFORM"); // test hook: every monster in that colour map (1-30)
                renderer.others.push_back({q[0],q[1],uint8_t(e.kind),e.facing,e.classId,e.mode,uint32_t(e.id-1000),e.stateTicks,e.animSpeed,e.states,e.components,forced&&e.kind==d2::EntityKind::Enemy?uint8_t(std::atoi(forced)):e.colorTransform,e.monsterType,e.lookClass,e.lookMode,e.look,
                    e.sequenceMode,e.sequenceFrame,e.skillMoveFrame,e.usedSkill,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne,float(e.previous.x)/d2::fixedOne,float(e.previous.y)/d2::fixedOne});}
                // Inferno and Arctic Blast (cltdofunc 24, D2Client 0x6FACA1D0): each flame of the client's stream is cltmissilea or
                // cltmissileb on a coin toss. The server's stream (published, maxLife carries its skill) has only cltmissilea, so
                // half its flames, chosen by number, are drawn as cltmissileb.
                if(e.kind==d2::EntityKind::Missile&&skillTable&&missileTable)if(const auto* skill=skillTable->find(uint32_t(e.maxLife));skill&&skill->clientDo==24)
                    if(int(e.classId)==missileTable->rowOf(skill->clientMissiles[0])&&((uint32_t(e.id)*2654435761u)>>16)&1)
                        if(const int other=missileTable->rowOf(skill->clientMissiles[1]);other>=0)renderer.others.back().classId=uint32_t(other);
            }
            // The client's own effects (explosions where missiles ended), drawn like missiles at their animation speed.
            for(size_t i=0;i<clientEffects.size();++i) {
                const auto& fx=clientEffects[i];
                const auto* info=missileTable?missileTable->find(fx.classId):nullptr;
                const float age=float(worldView.tick-fx.born)+alpha;
                float px=fx.x+fx.vx*age,py=fx.y+fx.vy*age,fvx=fx.vx,fvy=fx.vy;
                if(fx.fallFrames>0) {
                    // Screen pixels back into subtiles: 16 px of screen Y is one step down both axes, 32 px of screen X
                    // is one step of x minus y. Height lifts the sprite; the slide starts it up-left of the marker.
                    const float remain=std::max(0.f,float(fx.fallFrames)-age);
                    const float height=remain*float(fx.fallRate),slide=remain*float(fx.slideRate);
                    px=fx.x-height/16.f-slide/32.f;py=fx.y-height/16.f+slide/32.f;
                    fvx=float(fx.fallRate)/16.f+float(fx.slideRate)/32.f;fvy=float(fx.fallRate)/16.f-float(fx.slideRate)/32.f;
                }
                const unsigned facing=fx.facing>=0?unsigned(fx.facing):(fvx||fvy?unsigned(((int(std::lround((std::atan2(fvy,fvx)*180/3.14159265-45)/22.5))%16)+16)%16):0);
                const int rate=fx.animRate>0?fx.animRate:(info?info->animRate:1024);
                renderer.others.push_back({px,py,uint8_t(d2::EntityKind::Missile),facing,fx.classId,0,uint32_t(0x70000000u+i),worldView.tick-fx.born+fx.tickOffset,uint16_t(std::max(1,rate/4))});
            }
            for(const auto& o:worldView.objects) {
                const auto* info=objectTable?objectTable->find(o.classId):nullptr;
                if(o.mode>0&&o.mode<8&&persistentOpenedContainer(o.classId,info))openedObjectModes[o.id]=2;
                const uint32_t mode=o.mode?o.mode:(openedObjectModes.count(o.id)?openedObjectModes[o.id]:o.mode);
                const auto clock=objectClocks.find(o.id);
                uint64_t ticks=clock==objectClocks.end()?0:clock->second.ticks;
                if(mode!=o.mode)ticks=100000; // latched-open containers should draw on their final opened frame.
                renderer.others.push_back({float(o.x)+.5f,float(o.y)+.5f,5,0,o.classId,mode,o.id,ticks,uint16_t(info&&mode<8&&info->delta[mode]?info->delta[mode]:256)});
            }
            if(followPlayer) {
                // Put the character on a whole screen pixel, so the tile grid also lands on whole
                // pixels (odd window or scene sizes would otherwise put quad edges on pixel centres,
                // which hardware and WARP round differently).
                const auto r=clientRect();
                const auto anchor=characterAnchor(scene,renderer.view);
                const float originX=std::round(r.right*.5f-anchor[0]*zoom),originY=std::round(r.bottom*.5f-anchor[1]*zoom);
                panX=originX-r.right*.5f+scene.width*.5f*zoom;panY=originY-r.bottom*.5f+scene.height*.5f*zoom;
            }
        };
        if(walkGrid) {
            refreshView(0);
            std::cout<<(nativeSave.empty()?"Walk prototype":"Native D2Game")<<": 25 Hz simulation | right-click to walk; hold WASD to move; C collision (red blocked, green walkable); F follow camera.\n";
        }
        if(map&&!referenceMap)renderer.upload(scene);else renderer.show(image);
        if(!idleCharacter.frames.empty())renderer.uploadCharacters(idleCharacter,walkingCharacter);
        for(const auto& [mode,animation]:movementCharacters)renderer.uploadMovement(mode,animation);
        WEB_STATIC std::function<void(uint32_t)> setTitle;
        setTitle=[&](uint32_t index) {
            std::string detail=(tiles?(singleTile?"Tile ":"Atlas page "):"Frame ")+std::to_string(index+1)+"/"+std::to_string(count);
            if(map)detail=referenceMap?"CPU reference":"GPU tiles "+std::to_string(renderer.visibleCount)+"/"+std::to_string(scene.placements.size());
            if(walkGrid) {
                const auto here=d2::subtileOf(worldView.player().position);
                detail=std::string(!nativeSave.empty()?"D2Game ":"")+(markerOnly?"Marker ":appearance.token+" ")+std::to_string(here.x)+","+std::to_string(here.y)+" | "+walkStatus+" | WASD / Right-click: walk | C: collision | F: follow";
            }
            if(tiles&&singleTile) {
                const auto t=d2::decodeDt1(bytes,palette,index);
                detail+=" ["+std::to_string(t.mainIndex)+","+std::to_string(t.subIndex)+","+std::to_string(t.orientation)+"]";
            }
            const std::string title="D2Next x64 | "+asset+" | "+detail+" | "+renderer.backendName()+" | Drag: pan | Wheel: zoom | R: reset";
            window->setTitle(title);
        };
        setTitle(0);
        if(!smoke) window->show();
        if(std::getenv("D2MODERN_AUDIO_SELFTEST")) {
            // Test hook: a button click and the town music through the build's sound system (use SDL_AUDIO_DRIVER=dummy to
            // hear nothing); prints how many sounds play over two seconds.
            d2::Audio test(archives);
            test.effect("cursor\\button.wav");test.music("data\\global\\music\\Act1\\town1.wav");
            for(int i=0;i<=100;++i){test.update();if(i%20==0)std::cout<<"Audio self-test: "<<i*20<<" ms, "<<test.playing()<<" playing"<<std::endl;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            return 0;
        }
        if(smoke&&walkGrid&&std::getenv("D2MODERN_SOUND_LOG")) {
            // Test hook: the sounds a scripted run asks for are logged (Audio::effect) but not played.
            audio=std::make_unique<d2::Audio>(archives);audio->setEnabled(false);gameAudio=audio.get();
            for(const char* track:{"town1","wild"})audio->preload(std::string("data\\global\\music\\Act1\\")+track+".wav");
            soundMonsters=std::make_unique<d2::MonsterTable>(archives);soundBank=std::make_unique<d2::SoundBank>(archives,*soundMonsters);gameSounds=soundBank.get();
        }
        if(smoke) {
            if(walkGrid&&!openEscMenu&&!std::getenv("D2MODERN_LAN_JOIN")) { // (a LAN guest's first map is its own, not the host's)
                // Exercise screen picking through the same handler as a real click, then let the
                // fixed-step simulation carry the character there.
                const auto start=spawn;d2::GridPoint destination=start;size_t steps=0;
                for(int y=-5;y<=5&&destination==start;++y)for(int x=-5;x<=5;++x) {
                    d2::GridPoint candidate{start.x+x,start.y+y};
                    auto path=d2::findPath(collision,start,candidate);
                    if(path.size()>=3&&path.size()<=12){destination=candidate;steps=path.size();break;}
                }
                if(destination==start)throw std::runtime_error("Movement smoke test found no nearby route");
                const auto p=d2::projectSubtile(scene,destination);const auto rect=clientRect();
                const int sx=int((p[0]-scene.width*.5f)*zoom+panX+rect.right*.5f);
                const int sy=int((p[1]-scene.height*.5f)*zoom+panY+rect.bottom*.5f);
                scriptedClick(sx,sy);
                size_t ticks=0;
                do {runTick();++ticks;} while(worldView.player().moving&&ticks<2000);
                if(!(d2::subtileOf(worldView.player().position)==destination)||worldView.player().moving)throw std::runtime_error("Click-to-walk smoke test did not reach destination");
                const auto resting=worldView.player().position;
                scriptedClick(-30000,-30000);
                runTick();
                if(!(worldView.player().position==resting)||worldView.player().moving||walkStatus!="Blocked or unreachable")throw std::runtime_error("Out-of-map click moved the character");
                std::cout<<"Movement verified: "<<steps<<" subtile route walked in "<<ticks<<" ticks ("<<ticks*1000/d2::simulationHz<<" ms simulated); outside-map click rejected\n";
                if(nativeSave.empty()){world=d2::makePrototypeBackend(collision,spawn);worldView=world->snapshot();}
                walkStatus="Ready";
            }
            if(wantSkillMenu)skillMenu=2;
            // Scripted clicks (--click X Y): the interface is drawn once so that its regions exist, then clicked like a player would, and the game runs on.
            for(const auto& click:clicks) {
                renderer.draw();
                scriptedClick(click.x,click.y);
                for(int tick=0;tick<10;++tick)runTick();
                mouseX=click.x;mouseY=click.y;
            }
            if(walkGrid&&talkClass&&!nativeSave.empty()) {
                // Scripted conversation for captures: walk up to the person, wait for the menu, optionally open the store.
                int32_t guid=0;
                for(const auto& e:worldView.entities)if(e.classId==talkClass&&e.id!=worldView.localPlayer)guid=int32_t(e.id-1000);
                if(!guid)throw std::runtime_error("--talk: that person is not in the level");
                pendingCommands.push_back(d2::Command::interact(guid));
                for(int tick=0;tick<1500&&!npcDialog.open;++tick)runTick();
                if(!npcDialog.open)throw std::runtime_error("--talk: the person never opened a conversation");
                if(const char* listen=std::getenv("D2MODERN_TALK_LISTEN");listen&&std::string(listen)=="show") {
                    // Capture the speech 8 seconds in, scrolling.
                    startTalk();for(int tick=0;tick<5;++tick)runTick();
                    std::this_thread::sleep_for(std::chrono::seconds(8));
                    std::cout<<"Talk: "<<npcDialog.talkSeconds<<" s, "<<talkRows().size()<<" rows\n";
                } else if(std::getenv("D2MODERN_TALK_LISTEN")) {
                    // Choose Talk, then close the conversation, as a player who heard the NPC out (quests move on).
                    for(const auto id:npcDialog.lines)std::cout<<"Line "<<id<<"\n";
                    startTalk();std::cout<<"Said "<<npcDialog.speaking<<(npcDialog.gossip?" (gossip)":"")<<": "<<(gameStrings?gameStrings->keyById(npcDialog.speaking):std::string())<<"\n";
                    for(int tick=0;tick<20;++tick)runTick();
                    closeNpcDialog();
                    for(int tick=0;tick<20;++tick)runTick();
                }
                if(talkShop) {
                    pendingCommands.push_back(d2::Command::raw(0x2F|(9<<8),1,npcDialog.guid,0,0)); // as the menu's Trade sends it (Input.cpp)
                    pendingCommands.push_back(d2::Command::raw(0x38|(13<<8),1,npcDialog.guid,0,0));
                    npcDialog.page=NpcDialog::Page::Shop;npcDialog.tab=1;inventoryOpen=true;
                    for(int tick=0;tick<60;++tick)runTick();
                    // Test hook: D2MODERN_SHOP_BUY="page,page...": buy the cheapest affordable item of each store page in turn as a
                    // click on it does (Input.cpp, region 22), and report where the game put it.
                    std::istringstream buyPages(std::getenv("D2MODERN_SHOP_BUY")?std::getenv("D2MODERN_SHOP_BUY"):"");
                    for(std::string buyPage;std::getline(buyPages,buyPage,',');) {
                        const d2::ItemSnapshot* offer=nullptr;
                        for(const auto& item:worldView.items)if(item.vendor==uint32_t(npcDialog.guid)&&item.page==uint32_t(std::atoi(buyPage.c_str()))&&item.price<=uint32_t(worldView.vitals.gold)&&(!offer||item.price<offer->price))offer=&item;
                        if(!offer)throw std::runtime_error("D2MODERN_SHOP_BUY: nothing affordable on that page");
                        std::set<uint32_t> before;for(const auto& item:worldView.items)if(!item.vendor)before.insert(item.id);
                        const uint32_t code=offer->code,price=offer->price,offered=offer->id;
                        pendingCommands.push_back(d2::Command::raw(0x32|(17<<8),npcDialog.guid,int32_t(offered),0,int32_t(price)));boughtItemCode=code; // as the store click does (Input.cpp)
                        for(int tick=0;tick<40;++tick)runTick();
                        std::cout<<"Shop buy: "<<d2::ItemTable::codeString(code)<<" for "<<price<<", gold now "<<worldView.vitals.gold<<"\n";
                        bool still=false;for(const auto& item:worldView.items)still|=item.id==offered;
                        std::cout<<"Shop buy: the store's copy is "<<(still?"still there":"gone")<<"\n";
                        for(const auto& item:worldView.items)if(!item.vendor&&!before.count(item.id))
                            std::cout<<"Shop buy: new item "<<item.id<<" "<<d2::ItemTable::codeString(item.code)<<" mode "<<item.mode<<" page "<<item.page<<" body "<<item.bodyLoc<<" at "<<item.x<<","<<item.y<<"\n";
                    }
                }
                if(const char* hire=std::getenv("D2MODERN_HIRE")) {
                    // Test hook: open the Hire page and list the offers; D2MODERN_HIRE=N then hires the Nth (from 1) as a click does.
                    npcDialog.page=NpcDialog::Page::Hire;
                    for(int tick=0;tick<5;++tick)runTick();
                    for(const auto& o:worldView.hirelings)
                        std::cout<<"Hireling "<<(strings?strings->byId(o.name):std::to_string(o.name))<<": level "<<o.level<<", life "<<o.life<<", defense "<<o.defense<<", cost "<<o.cost
                                 <<", damage "<<o.minDamage<<"-"<<o.maxDamage<<", resist "<<o.resist<<", special "<<(strings&&o.description>0?strings->byId(uint32_t(o.description)):std::string("-"))<<"\n";
                    if(const int pick=std::atoi(hire);pick>0&&size_t(pick)<=worldView.hirelings.size()) {
                        pendingCommands.push_back(d2::Command::cheat(0,10000));for(int tick=0;tick<3;++tick)runTick(); // gold to pay with
                        const auto gold=worldView.vitals.gold;
                        pendingCommands.push_back(d2::Command::raw(0x36|(9<<8),npcDialog.guid,worldView.hirelings[size_t(pick-1)].name,0,0));
                        closeNpcDialog();
                        for(int tick=0;tick<40;++tick)runTick();
                        std::cout<<"Hired: gold "<<gold<<" -> "<<worldView.vitals.gold<<", offers left "<<worldView.hirelings.size()<<"\n";
                        if(std::getenv("D2MODERN_HIRE_PANEL")) {
                            // Test hook: a carried cap (D2MODERN_TEST_GIVE=cap) onto the hireling's head (0x19 to the cursor, then 0x61
                            // with body location 1), and its panel open with the inventory for the capture.
                            int32_t cap=0;for(const auto& item:worldView.items)if(!item.vendor&&item.mode==0&&d2::ItemTable::codeString(item.code)=="cap")cap=int32_t(item.id);
                            if(cap){pendingCommands.push_back(d2::Command::raw(0x19|(5<<8),cap,0,0,0));for(int tick=0;tick<6;++tick)runTick();
                                pendingCommands.push_back(d2::Command::raw(0x61|(3<<8),1,0,0,0));for(int tick=0;tick<10;++tick)runTick();}
                            for(const auto& item:worldView.items)if(item.vendor==worldView.hireling.guid&&worldView.hireling.guid)
                                std::cout<<"Hireling wears "<<d2::ItemTable::codeString(item.code)<<" at "<<item.bodyLoc<<" (mode "<<item.mode<<")\n";
                            const auto& h=worldView.hireling;
                            std::cout<<"Hireling sheet: level "<<h.level<<", exp "<<h.experience<<"/"<<h.nextExperience<<", str "<<h.strength<<", dex "<<h.dexterity<<", life "<<h.life<<"/"<<h.maxLife<<", def "<<h.defense<<", damage "<<h.minDamage<<"-"<<h.maxDamage<<", resists "<<h.resist[0]<<"/"<<h.resist[1]<<"/"<<h.resist[2]<<"/"<<h.resist[3]<<"\n";
                            hirelingOpen=true;inventoryOpen=true;
                        }
                        if(std::getenv("D2MODERN_HIRE_KILL")) {
                            // Then the hireling dies (host cheat 9) and is brought back at the same NPC (0x62).
                            pendingCommands.push_back(d2::Command::cheat(9));
                            for(int tick=0;tick<80;++tick)runTick();
                            std::cout<<"Hireling died: resurrect "<<(strings&&deadMercName>=0?strings->byId(uint32_t(deadMercName)):std::string("-"))<<" for "<<resurrectCost<<", pets "<<pets.size()<<"\n";
                            pendingCommands.push_back(d2::Command::interact(guid));
                            for(int tick=0;tick<300&&!npcDialog.open;++tick)runTick();
                            const auto before=worldView.vitals.gold;
                            pendingCommands.push_back(d2::Command::raw(0x62|(5<<8),npcDialog.guid,0,0,0));closeNpcDialog();
                            for(int tick=0;tick<40;++tick)runTick();
                            std::cout<<"Resurrected: gold "<<before<<" -> "<<worldView.vitals.gold<<", dead hireling "<<deadMercName<<", pets "<<pets.size()<<"\n";
                        }
                    }
                    if(std::string(hire)=="0")for(int tick=0;tick<3;++tick)runTick(); // leave the page open for a capture
                }
                if(talkShop&&std::getenv("D2MODERN_TEST_REPAIR")) {
                    // Test hook: wear everything down (host cheat 10), repair one worn item as Repair mode's click does, then the rest.
                    pendingCommands.push_back(d2::Command::cheat(0,10000));pendingCommands.push_back(d2::Command::cheat(10));
                    for(int tick=0;tick<5;++tick)runTick();
                    int32_t worn=0;for(const auto& item:worldView.items)if(!item.vendor&&item.mode==1&&!worn)worn=int32_t(item.id);
                    auto gold=worldView.vitals.gold;
                    pendingCommands.push_back(d2::Command::raw(0x35|(17<<8),npcDialog.guid,worn,0,0));
                    for(int tick=0;tick<20;++tick)runTick();
                    std::cout<<"Repair one (item "<<worn<<"): gold "<<gold<<" -> "<<worldView.vitals.gold<<"\n";
                    gold=worldView.vitals.gold;
                    pendingCommands.push_back(d2::Command::raw(0x35|(17<<8),npcDialog.guid,0,0,-1));
                    for(int tick=0;tick<20;++tick)runTick();
                    std::cout<<"Repair all: gold "<<gold<<" -> "<<worldView.vitals.gold<<"\n";
                }
                if(std::getenv("D2MODERN_TALK_AGAIN")) {
                    // Test hook: after the first conversation (D2MODERN_TALK_LISTEN), talk to the person again (quests move on).
                    if(npcDialog.open)closeNpcDialog();
                    for(int tick=0;tick<20;++tick)runTick();
                    pendingCommands.push_back(d2::Command::interact(guid));
                    for(int tick=0;tick<600&&!npcDialog.open;++tick)runTick();
                    for(const auto id:npcDialog.lines)std::cout<<"Line again "<<id<<"\n";
                    if(npcDialog.open) {
                        // What Talk says now (the NPC's gossip once the game has nothing more), and what Introduction says.
                        startTalk();std::cout<<"Said again "<<npcDialog.speaking<<(npcDialog.gossip?" (gossip)":"")<<": "<<(strings?strings->keyById(npcDialog.speaking):std::string())<<"\n";
                        finishTalk();startIntroduction();std::cout<<"Introduction "<<npcDialog.speaking<<": "<<(strings?strings->keyById(npcDialog.speaking):std::string())<<"\n";
                        finishTalk();
                    }
                }
                std::cout<<"Conversation with class "<<talkClass<<": "<<npcDialog.lines.size()<<" lines, services "<<int(npcDialog.services)<<"\n";
                for(const auto topic:npcDialog.topics)std::cout<<"Topic "<<topic<<": "<<(strings?strings->byId(d2::topicCaption(topic)):std::string())<<"\n";
            }
            if(walkGrid&&wantWaypoint&&!nativeSave.empty()) {
                // Scripted capture: open the nearest waypoint object's menu.
                int32_t guid=0;
                for(const auto& o:worldView.objects)if(objectTable&&objectTable->find(o.classId)&&objectTable->find(o.classId)->name=="Waypoint")guid=int32_t(o.id);
                if(!guid)throw std::runtime_error("--open-waypoint: no waypoint object nearby");
                pendingCommands.push_back(d2::Command::interactObject(guid));
                for(int tick=0;tick<400&&!waypointMenu.open;++tick)runTick();
                if(!waypointMenu.open)throw std::runtime_error("--open-waypoint: the waypoint menu never opened");
                std::cout<<"Waypoint menu opened (object "<<guid<<")\n";
            }
            if(walkGrid&&autoplay&&!nativeSave.empty()) {
                // Scripted play for captures: walk the encampment's south route into the Blood Moor, then fight until loot lies on the ground.
                const d2::GridPoint route[]={{5812,4474},{5814,4506},{5814,4530},{5814,4580},{5814,4610},{5830,4608},{5838,4624},{5840,4656},{5846,4688},{5870,4720}};
                for(const auto& waypoint:route) {
                    for(int hop=0;hop<8;++hop) {
                        const auto here=d2::subtileOf(worldView.player().position);
                        const d2::GridPoint target{waypoint.x-nativeOrigin.x,waypoint.y-nativeOrigin.y};
                        const int dx=target.x-here.x,dy=target.y-here.y,span=std::max(std::abs(dx),std::abs(dy));
                        if(span<=1)break;
                        const int step=std::min(span,14);
                        d2::GridPoint next{here.x+dx*step/span,here.y+dy*step/span};
                        if(!collision.walkable(next))next=target;
                        pendingCommands.push_back(d2::Command::moveTo(next));
                        size_t waited=0;do {runTick();++waited;} while((waited<6||worldView.player().moving)&&waited<400);
                    }
                }
                // D2MODERN_STOP_AT_MODE=N stops the fight the moment the player is in animation mode N (7 attack, 4 get hit, ...),
                // so the capture shows that animation.
                const char* stopAt=std::getenv("D2MODERN_STOP_AT_MODE");
                std::set<int> modesSeen;
                for(int tick=0;tick<autoplayTicks;++tick) {
                    modesSeen.insert(worldView.player().mode);
                    if(stopAt&&worldView.player().mode==std::atoi(stopAt)) {
                        // D2MODERN_STOP_DELAY=N: N ticks further into that animation, while it lasts.
                        const int delay=std::getenv("D2MODERN_STOP_DELAY")?std::atoi(std::getenv("D2MODERN_STOP_DELAY")):0;
                        for(int t=0;t<delay;++t){runTick();if(worldView.player().mode!=std::atoi(stopAt))break;}
                        break;
                    }
                    bool ground=false;for(const auto& i:worldView.items)if(i.mode==3||i.mode==5)ground=true;
                    if(ground&&!stopAt){if(audio)for(int t=0;t<30;++t){runTick();refreshView(0);}break;} // with sounds: let the item land
                    if(tick%50==0) {
                        const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
                        for(const auto& e:worldView.entities) {
                            if(e.kind!=d2::EntityKind::Enemy)continue;
                            const double dx=(e.position.x-worldView.player().position.x)/double(d2::fixedOne),dy=(e.position.y-worldView.player().position.y)/double(d2::fixedOne);
                            if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&e;}
                        }
                        if(nearest)pendingCommands.push_back(d2::Command::attack(int32_t(nearest->id-1000)));
                    }
                    runTick();
                    if(audio)refreshView(0); // the fight's sounds (heard, or logged with D2MODERN_SOUND_LOG)
                }
                std::cout<<"Autoplay: level "<<worldView.level<<", "<<worldView.items.size()<<" items, life "<<worldView.vitals.life<<"/"<<worldView.vitals.maxLife<<"\n";
                std::cout<<"Player modes seen:";
                for(int m:modesSeen)std::cout<<" "<<m<<(m==5||m==6||movementCharacters.count(uint8_t(m))?"":" (no art)");
                std::cout<<"\n";
            }
            if(walkGrid&&travelLevel&&!nativeSave.empty()&&objectTable) {
                // Scripted capture: open the encampment waypoint and travel to another level (needs D2MODERN_TEST_WAYPOINTS=1 for discovered waypoints).
                uint32_t waypointGuid=0;
                for(const auto& o:worldView.objects){const auto* info=objectTable->find(o.classId);if(info&&info->name=="Waypoint")waypointGuid=o.id;}
                if(waypointGuid) {
                    pendingCommands.push_back(d2::Command::interactObject(int32_t(waypointGuid)));
                    for(int t=0;t<150;++t)runTick();
                    pendingCommands.push_back(d2::Command::raw(0x49|(9<<8),int32_t(waypointGuid),int32_t(travelLevel),0,0));
                    for(int t=0;t<300&&worldView.level!=travelLevel;++t)runTick();
                    for(int t=0;t<60;++t)runTick();
                }
                std::cout<<"Travel: now in level "<<worldView.level<<"\n";
            }
            if(walkGrid&&walkWorld&&!nativeSave.empty()) {
                // Scripted capture: walk to a world position by pathfinding over the scene, in hops the game's pathfinder reaches.
                for(int t=0;t<6000;++t) {
                    const auto here=d2::subtileOf(worldView.player().position);
                    d2::GridPoint goal{walkWorldX-nativeOrigin.x,walkWorldY-nativeOrigin.y};
                    for(int r=1;r<=20&&!collision.walkable(goal);++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx)if(collision.walkable({walkWorldX-nativeOrigin.x+dx,walkWorldY-nativeOrigin.y+dy})){goal={walkWorldX-nativeOrigin.x+dx,walkWorldY-nativeOrigin.y+dy};dx=dy=r+1;}
                    if(std::abs(here.x-goal.x)<=2&&std::abs(here.y-goal.y)<=2)break;
                    if(t%15==0){auto path=d2::findPath(collision,here,goal);if(path.size()>=2)pendingCommands.push_back(d2::Command::moveTo(path[std::min<size_t>(path.size()-1,12)]));}
                    runTick();
                }
                for(int t=0;t<30;++t)runTick();
            }
            // Clicks after the scripted travel/walk, then the game runs on (a level entrance loads its new scene meanwhile).
            for(const auto& click:lateClicks) {
                refreshView(1.f);renderer.draw();
                scriptedClick(click.x,click.y);
                for(int tick=0;tick<300;++tick)runTick();
                std::cout<<"After the click: level "<<worldView.level<<"\n";
            }
            if(walkGrid&&castSkillId&&!nativeSave.empty()) {
                // Scripted capture: learn a skill when the character has a point to spend, select it, then cast it at the
                // nearest enemy repeatedly, stopping the instant a missile entity appears so the capture frame catches it mid-flight.
                auto skillLevelOf=[&](uint32_t id){for(const auto& skill:worldView.skills)if(skill.id==id)return skill.level;return 0;};
                // D2MODERN_NEXT_APPROACH: one step toward a far enemy. A closed door (objects.txt IsDoor, mode 0) next to the next
                // 40 steps of the route is opened first: the server blocks closed doors, the collision grid doesn't hold them.
                // How close D2MODERN_NEXT_APPROACH walks before using the skill (D2MODERN_APPROACH_RANGE, subtiles; 20 by default).
                const double approachRange=std::getenv("D2MODERN_APPROACH_RANGE")?std::atof(std::getenv("D2MODERN_APPROACH_RANGE")):20.0;
                // Whether the straight line from the player to a unit crosses only walkable subtiles (no wall between).
                auto clearLine=[&](d2::FixedPoint to) {
                    const auto a=d2::subtileOf(worldView.player().position),b=d2::subtileOf(to);
                    const int steps=std::max(std::abs(b.x-a.x),std::abs(b.y-a.y))*2;
                    for(int i=1;i<steps;++i)if(!collision.walkable({a.x+int(std::lround(double(b.x-a.x)*i/steps)),a.y+int(std::lround(double(b.y-a.y)*i/steps))}))return false;
                    return true;
                };
                auto approach=[&](d2::GridPoint goal) {
                    const auto here=d2::subtileOf(worldView.player().position);
                    const auto path=d2::findPath(collision,here,goal);
                    if(objectTable)for(const auto& o:worldView.objects)if(const auto* info=objectTable->find(o.classId);info&&info->door&&o.mode==0)
                        for(size_t i=0;i<std::min<size_t>(path.size(),40);++i)if(std::abs(path[i].x-o.x)<=3&&std::abs(path[i].y-o.y)<=3) {
                            pendingCommands.push_back(d2::Command::interactObject(int32_t(o.id)));
                            std::cout<<"Approach: opening door "<<o.id<<" (class "<<o.classId<<") at "<<o.x<<","<<o.y<<"\n";
                            return;
                        }
                    pendingCommands.push_back(d2::Command::moveTo(goal));
                    std::cout<<"Approach: "<<path.size()<<" steps to go (goal walkable "<<collision.walkable(goal)<<"), tick "<<worldView.tick<<"\n";
                };
                if(const char* wield=std::getenv("D2MODERN_TEST_WIELD");wield&&*wield) {
                    // Test hook: "right[,left]" item codes (given first with D2MODERN_TEST_GIVE) put in the hands, for the skills
                    // a weapon gates (bow skills, Double Swing, Dragon Claw...). Both hands are emptied first (picked up from
                    // the body, 0x1C, and dropped, 0x17); each item is taken from the inventory (0x19) and put on (0x1A).
                    auto wait=[&]{for(int t=0;t<6;++t)runTick();};
                    for(uint32_t loc:{5u,4u})for(const auto& item:worldView.items)if(item.mode==1&&item.bodyLoc==loc&&!item.vendor) {
                        const int32_t id=int32_t(item.id);
                        pendingCommands.push_back(d2::Command::raw(0x1C|(3<<8),int32_t(loc),0,0,0));wait();
                        pendingCommands.push_back(d2::Command::raw(0x17|(5<<8),id,0,0,0));wait();
                        break;
                    }
                    std::istringstream codes(wield);std::string code;std::set<uint32_t> used;uint32_t loc=4;
                    while(std::getline(codes,code,',')&&loc<=5) {
                        for(const auto& item:worldView.items)if(item.mode==0&&!item.vendor&&!used.count(item.id)&&d2::ItemTable::codeString(item.code)==code) {
                            used.insert(item.id);const int32_t id=int32_t(item.id);
                            pendingCommands.push_back(d2::Command::raw(0x19|(5<<8),id,0,0,0));wait();
                            pendingCommands.push_back(d2::Command::raw(0x1A|(9<<8),id,int32_t(loc),0,0));wait();
                            break;
                        }
                        ++loc;
                    }
                    std::cout<<"Wielding:";
                    for(const auto& item:worldView.items)if(item.mode==1&&(item.bodyLoc==4||item.bodyLoc==5)&&!item.vendor)std::cout<<" "<<d2::ItemTable::codeString(item.code)<<"@"<<item.bodyLoc;
                    std::cout<<"\n";
                }
                if(skillLevelOf(castSkillId)<=0&&worldView.stats[5]>0) {
                    pendingCommands.push_back(d2::Command::raw(0x3B|(3<<8),int32_t(castSkillId),0,0,0));
                    for(int t=0;t<5;++t)runTick();
                }
                const bool skillKnown=skillLevelOf(castSkillId)>0;
                // D2MODERN_STAT_LOG=1: the player's vitals and character sheet before the cast and after it (a buff's stats).
                auto statLog=[&](const char* when) {
                    if(!std::getenv("D2MODERN_STAT_LOG"))return;
                    const auto& v=worldView.vitals;const auto& s=worldView.sheet;
                    std::cout<<"Stats "<<when<<": skill "<<castSkillId<<" level "<<skillLevelOf(castSkillId)<<", life "<<v.life<<"/"<<v.maxLife<<", mana "<<v.mana<<"/"<<v.maxMana
                        <<", defense "<<s.defense<<", attack "<<s.attackRating<<", damage "<<s.damageMin<<"-"<<s.damageMax
                        <<", resist "<<s.resist[0]<<"/"<<s.resist[1]<<"/"<<s.resist[2]<<"/"<<s.resist[3]<<", strength "<<worldView.stats[0]<<", states";
                    for(uint32_t st=0;st<192;++st)if(worldView.player().states[st>>5]&(1u<<(st&31)))std::cout<<" "<<st;
                    std::cout<<"\n";
                };
                statLog("before");
                if(skillKnown) {
                    pendingCommands.push_back(d2::Command::raw(0x3C|(9<<8),int32_t(castSkillId),-1,0,0));
                    for(int t=0;t<5;++t)runTick();
                } else std::cout<<"Cast skill "<<castSkillId<<": not learned and no skill point is available\n";
                bool sawMissile=false;
                // D2MODERN_CAST_TICKS=N keeps casting for N ticks and reports what happened (modes, mana, missiles);
                // with D2MODERN_STOP_AT_MODE (and _DELAY) it stops the moment the player is in that mode.
                const int castTicks=std::getenv("D2MODERN_CAST_TICKS")?std::atoi(std::getenv("D2MODERN_CAST_TICKS")):0;
                const char* stopAt=std::getenv("D2MODERN_STOP_AT_MODE");
                std::set<int> castModes;const int manaBefore=worldView.vitals.mana;size_t mostMissiles=0;
                for(int tick=0;tick<(castTicks?castTicks:2000)&&(castTicks||!sawMissile);++tick) {
                    castModes.insert(worldView.player().mode);
                    size_t missiles=0;for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Missile)++missiles;
                    mostMissiles=std::max(mostMissiles,missiles);
                    // D2MODERN_STOP_MISSILE_AGE=N: stop N ticks after the first missile appears (to see it in flight).
                    static int missileSince=-1;
                    if(missiles&&missileSince<0)missileSince=tick;
                    if(std::getenv("D2MODERN_STOP_MISSILE_AGE")&&missileSince>=0&&tick-missileSince>=std::atoi(std::getenv("D2MODERN_STOP_MISSILE_AGE")))break;
                    if(stopAt&&worldView.player().mode==std::atoi(stopAt)) {
                        const int delay=std::getenv("D2MODERN_STOP_DELAY")?std::atoi(std::getenv("D2MODERN_STOP_DELAY")):0;
                        for(int t=0;t<delay&&worldView.player().mode==std::atoi(stopAt);++t)runTick();
                        break;
                    }
                    if(skillKnown&&tick%20==0) {
                        if(castSkillId==uint32_t(d2::skillLeap)) {
                            const auto here=d2::subtileOf(worldView.player().position);
                            std::optional<d2::GridPoint> destination;
                            for(int radius=16;radius>=3&&!destination;--radius)for(int dy=-radius;dy<=radius&&!destination;++dy)for(int dx=-radius;dx<=radius;++dx) {
                                if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;
                                const d2::GridPoint candidate{here.x+dx,here.y+dy};
                                if(collision.walkable(candidate)){destination=candidate;break;}
                            }
                            if(destination)pendingCommands.push_back(d2::Command::castAt(true,*destination));
                            runTick();
                            if(audio||std::getenv("D2MODERN_LEAP_LOG")||std::getenv("D2MODERN_SEQUENCE_LOG"))refreshView(0);
                            continue;
                        }
                        // D2MODERN_CAST_AT=dx,dy: cast at that spot from the player instead (a curse with no enemy near, indoors).
                        if(const char* at=std::getenv("D2MODERN_CAST_AT")) {
                            int dx=0,dy=0;
                            if(std::sscanf(at,"%d,%d",&dx,&dy)==2){const auto here=d2::subtileOf(worldView.player().position);pendingCommands.push_back(d2::Command::castAt(true,{here.x+dx,here.y+dy}));}
                            runTick();
                            continue;
                        }
                        const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
                        for(const auto& e:worldView.entities) {
                            if(e.kind!=d2::EntityKind::Enemy||!collision.walkable(d2::subtileOf(e.position)))continue; // not a trap in a wall (gargoyletrap)
                            if(const char* only=std::getenv("D2MODERN_TARGET_CLASS");only&&e.classId!=uint32_t(std::atoi(only)))continue; // test: aim at one monster class only
                            const double dx=(e.position.x-worldView.player().position.x)/double(d2::fixedOne),dy=(e.position.y-worldView.player().position.y)/double(d2::fixedOne);
                            if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&e;}
                        }
                        // D2MODERN_NEXT_APPROACH=1 here too: walk within 20 subtiles before casting (Dragon Flight's range).
                        if(nearest&&std::getenv("D2MODERN_NEXT_APPROACH")&&(best>approachRange*approachRange||!clearLine(nearest->position))) {
                            std::cout<<"Approach: nearest enemy "<<std::sqrt(best)<<" subtiles away\n";
                            approach(d2::subtileOf(nearest->position));
                        }
                        else if(nearest)pendingCommands.push_back(d2::Command::castOn(true,int32_t(nearest->id-1000)));
                    }
                    runTick();
                    if(audio||std::getenv("D2MODERN_LEAP_LOG")||std::getenv("D2MODERN_SEQUENCE_LOG"))refreshView(0); // lets the sounds of the cast be heard (or logged)
                    for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Missile)sawMissile=true;
                }
                std::cout<<"Cast skill "<<castSkillId<<(sawMissile?": a missile is in flight":": no missile appeared")<<"\n";
                statLog("after");
                if(const char* watch=std::getenv("D2MODERN_WATCH_TICKS")) {
                    // Test hook: keep the game running N ticks after the cast and report, for each enemy, its distance from the
                    // player and its life at the start and the end, the modes it went through and its states at the start
                    // (a behaviour curse: Terror flees, Confuse and Attract fight each other, Iron Maiden hurts its attacker).
                    struct Watched {double from=0,to=0;int32_t lifeFrom=0,lifeTo=0;uint32_t classId=0;std::set<int> modes;std::array<uint32_t,6> states{};bool seen=false;};
                    std::map<d2::EntityId,Watched> watched;
                    auto distance=[&](const d2::EntitySnapshot& e){const double dx=double(e.position.x-worldView.player().position.x)/d2::fixedOne,dy=double(e.position.y-worldView.player().position.y)/d2::fixedOne;return std::sqrt(dx*dx+dy*dy);};
                    for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy)watched[e.id]={distance(e),distance(e),e.life,e.life,e.classId,{int(e.mode)},e.states,true};
                    // D2MODERN_TEST_HURT=pct: the player's life drops to pct% first (Life Tap needs room to heal).
                    // D2MODERN_WATCH_ATTACK=1: attack the nearest enemy every 20 ticks meanwhile (Life Tap, Iron Maiden).
                    // D2MODERN_WATCH_WALK=dx,dy: walk to that offset meanwhile and report the player's speed (Burst of Speed).
                    if(const char* hurt=std::getenv("D2MODERN_TEST_HURT")){pendingCommands.push_back(d2::Command::cheat(6,std::atoi(hurt)));runTick();}
                    // D2MODERN_TEST_NODEFENSE=1: the player is easy to hit (level 1, defense 0; absorb tests).
                    if(const char* bare=std::getenv("D2MODERN_TEST_NODEFENSE")){pendingCommands.push_back(d2::Command::cheat(7,std::atoi(bare)));runTick();}
                    const int32_t playerLife=worldView.vitals.life,playerMana=worldView.vitals.mana;
                    const auto walkFrom=worldView.player().position;int walkTicks=-1;
                    if(const char* walkTo=std::getenv("D2MODERN_WATCH_WALK")) {
                        int dx=0,dy=0;
                        if(std::sscanf(walkTo,"%d,%d",&dx,&dy)==2){const auto here=d2::subtileOf(walkFrom);pendingCommands.push_back(d2::Command::moveTo({here.x+dx,here.y+dy}));walkTicks=0;}
                    }
                    double walked=0;
                    for(int t=0;t<std::atoi(watch);++t) {
                        if(std::getenv("D2MODERN_WATCH_ATTACK")&&t%20==0) {
                            const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
                            for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy){const double d=distance(e);if(d<best){best=d;nearest=&e;}}
                            if(nearest)pendingCommands.push_back(d2::Command::attack(int32_t(nearest->id-1000)));
                        }
                        const auto before=worldView.player().position;
                        runTick();
                        if(audio)refreshView(0); // the sounds of what happens meanwhile (a state's loop)
                        if(walkTicks>=0&&worldView.player().moving){++walkTicks;walked+=std::hypot(double(worldView.player().position.x-before.x),double(worldView.player().position.y-before.y))/d2::fixedOne;}
                        for(const auto& e:worldView.entities)if(auto w=watched.find(e.id);w!=watched.end()) {
                            w->second.to=distance(e);w->second.lifeTo=e.life;w->second.modes.insert(int(e.mode));
                        }
                    }
                    std::cout<<"Watch: player life "<<playerLife<<" -> "<<worldView.vitals.life<<", mana "<<playerMana<<" -> "<<worldView.vitals.mana<<", defense "<<worldView.sheet.defense<<"\n";
                    if(walkTicks>0)std::cout<<"Watch: walked "<<walked<<" subtiles in "<<walkTicks<<" moving ticks ("<<walked/walkTicks<<" a tick)\n";
                    for(const auto& [id,w]:watched) {
                        std::cout<<"Watch: enemy "<<id-1000<<" class "<<w.classId<<", distance "<<std::lround(w.from)<<" -> "<<std::lround(w.to)<<", life "<<w.lifeFrom<<" -> "<<w.lifeTo<<", modes";
                        for(int m:w.modes)std::cout<<" "<<m;
                        std::cout<<", states";
                        for(uint32_t s=0;s<192;++s)if(w.states[s>>5]&(1u<<(s&31)))std::cout<<" "<<s;
                        std::cout<<"\n";
                    }
                }
                if(castTicks) {
                    std::cout<<"Casting: mana "<<manaBefore<<" -> "<<worldView.vitals.mana<<", at most "<<mostMissiles<<" missiles at once, player modes:";
                    for(int m:castModes)std::cout<<" "<<m<<(m==5||m==6||movementCharacters.count(uint8_t(m))?"":" (no art)");
                    std::cout<<"\n";
                    if(const auto* skill=skillTable->find(uint32_t(castSkillId));skill&&!skill->castOverlay.empty())
                        if(const auto* info=overlayTable->find(skill->castOverlay))
                            std::cout<<"Cast overlay "<<skill->castOverlay<<": "<<info->file<<", "<<info->frames<<" frames, offset "<<info->x<<","<<info->y<<", heights "<<info->height[0]<<"/"<<info->height[1]<<"/"<<info->height[2]<<"/"<<info->height[3]<<", predraw "<<info->preDraw<<", rate "<<info->rate<<", trans "<<info->trans<<"\n";
                }
                if(const char* itemSkill=std::getenv("D2MODERN_ITEM_SKILL");itemSkill&&*itemSkill) {
                    // Test hook: drop the inventory item D2MODERN_ITEM_SKILL_DROP names (given with D2MODERN_TEST_GIVE), then
                    // use the skill on it (packet 0x0D, the right skill on a unit: type 4, the item): Telekinesis, Iron Golem.
                    auto wait=[&](int n){for(int t=0;t<n;++t)runTick();};
                    const char* drop=std::getenv("D2MODERN_ITEM_SKILL_DROP");int32_t target=0;
                    for(const auto& item:worldView.items)if(item.mode==0&&!item.vendor&&drop&&d2::ItemTable::codeString(item.code)==drop){target=int32_t(item.id);break;}
                    if(target) {
                        pendingCommands.push_back(d2::Command::raw(0x19|(5<<8),target,0,0,0));wait(6);
                        pendingCommands.push_back(d2::Command::raw(0x17|(5<<8),target,0,0,0));wait(6);
                        pendingCommands.push_back(d2::Command::raw(0x3C|(9<<8),std::atoi(itemSkill),-1,0,0));wait(5);
                        const size_t unitsBefore=worldView.entities.size();std::set<int> modes;
                        pendingCommands.push_back(d2::Command::raw(0x0D|(9<<8),4,target,0,0));
                        for(int t=0;t<80;++t){modes.insert(worldView.player().mode);runTick();}
                        std::string where="gone";
                        for(const auto& item:worldView.items)if(int32_t(item.id)==target)where=item.mode==3?"still on the ground":"mode "+std::to_string(item.mode);
                        std::cout<<"Item skill "<<itemSkill<<" on "<<drop<<": item "<<where<<", units "<<unitsBefore<<" -> "<<worldView.entities.size()<<", player modes:";
                        for(int m:modes)std::cout<<" "<<m;
                        std::cout<<"\n";
                    } else std::cout<<"Item skill "<<itemSkill<<": no "<<(drop?drop:"(none named)")<<" in the inventory\n";
                }
                if(const char* objectSkill=std::getenv("D2MODERN_OBJECT_SKILL");objectSkill&&*objectSkill) {
                    // Test hook: use the skill on the nearest object (packet 0x0D, type 2): Telekinesis on a waypoint, chest, shrine.
                    const d2::ObjectSnapshot* nearest=nullptr;double best=1e18;const auto here=d2::subtileOf(worldView.player().position);
                    const char* skip=std::getenv("D2MODERN_OBJECT_SKIP"); // an object class to pass over (119: the waypoint just arrived through)
                    for(const auto& o:worldView.objects){if(skip&&o.classId==uint32_t(std::atoi(skip)))continue;if(std::getenv("D2MODERN_OBJECT_NEUTRAL")&&o.mode!=0)continue; // only closed (neutral) objects
                        const double dx=o.x-here.x,dy=o.y-here.y;if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&o;}}
                    if(nearest) {
                        const uint32_t guid=nearest->id,classId=nearest->classId,modeBefore=nearest->mode;const int32_t ox=nearest->x,oy=nearest->y; // the list changes as the game runs
                        if(best>12.0*12.0) { // walk up to it first (Telekinesis reaches 25 subtiles)
                            pendingCommands.push_back(d2::Command::moveTo(d2::GridPoint{ox,oy}));
                            for(int t=0;t<150;++t){runTick();const auto p=d2::subtileOf(worldView.player().position);if(std::hypot(double(p.x-ox),double(p.y-oy))<10)break;}
                            pendingCommands.push_back(d2::Command::stop());for(int t=0;t<5;++t)runTick();
                            const auto p=d2::subtileOf(worldView.player().position);best=double(p.x-ox)*(p.x-ox)+double(p.y-oy)*(p.y-oy);
                        }
                        pendingCommands.push_back(d2::Command::raw(0x3C|(9<<8),std::atoi(objectSkill),-1,0,0));for(int t=0;t<5;++t)runTick();
                        pendingCommands.push_back(d2::Command::raw(0x0D|(9<<8),2,int32_t(guid),0,0));
                        std::set<int> modes;for(int t=0;t<60;++t){modes.insert(worldView.player().mode);runTick();}
                        uint32_t modeAfter=modeBefore;for(const auto& o:worldView.objects)if(o.id==guid)modeAfter=o.mode;
                        std::cout<<"Object skill "<<objectSkill<<" on object class "<<classId<<" at "<<std::sqrt(best)<<" subtiles: mode "<<modeBefore<<" -> "<<modeAfter<<(waypointMenu.open?", waypoint menu open":"")<<", player modes:";
                        for(int m:modes)std::cout<<" "<<m;
                        std::cout<<"\n";
                    }
                }
                const char* corpseSkill=std::getenv("D2MODERN_CORPSE_SKILL");const char* nextSkill=std::getenv("D2MODERN_NEXT_SKILL");
                if(const char* followSkill=corpseSkill&&*corpseSkill?corpseSkill:nextSkill;followSkill&&*followSkill) {
                    const bool onCorpse=followSkill==corpseSkill; // D2MODERN_NEXT_SKILL: the same on the nearest enemy (a Druid form skill after the form)
                    // Test hook: after the cast above has left corpses, use this skill (Raise Skeleton, Corpse Explosion, Revive,
                    // Find Item...) on the nearest corpse every 20 ticks for 120 ticks (D2MODERN_CORPSE_TICKS: fewer, to capture an
                    // effect), and report what changed.
                    const int skillId=std::atoi(followSkill);
                    auto count=[&](d2::EntityKind kind){size_t n=0;for(const auto& e:worldView.entities)if(e.kind==kind)++n;return n;};
                    const size_t corpsesBefore=count(d2::EntityKind::Corpse),unitsBefore=worldView.entities.size();
                    auto groundItems=[&]{size_t n=0;for(const auto& item:worldView.items)if(item.mode==3)++n;return n;};const size_t groundBefore=groundItems();
                    pendingCommands.push_back(d2::Command::raw(0x3C|(9<<8),skillId,-1,0,0));
                    for(int t=0;t<5;++t)runTick();
                    std::set<int> modes;size_t mostMissiles=0;int casts=0;
                    const int followTicks=std::getenv("D2MODERN_CORPSE_TICKS")?std::atoi(std::getenv("D2MODERN_CORPSE_TICKS")):120;
                    for(int tick=0;tick<followTicks;++tick) {
                        modes.insert(worldView.player().mode);
                        size_t missiles=count(d2::EntityKind::Missile);mostMissiles=std::max(mostMissiles,missiles);
                        if(tick%20==0) {
                            const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
                            for(const auto& e:worldView.entities)if(e.kind==(onCorpse?d2::EntityKind::Corpse:d2::EntityKind::Enemy)&&collision.walkable(d2::subtileOf(e.position))) {
                                const double dx=(e.position.x-worldView.player().position.x)/double(d2::fixedOne),dy=(e.position.y-worldView.player().position.y)/double(d2::fixedOne);
                                if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&e;}
                            }
                            // D2MODERN_NEXT_APPROACH=1: walk to within 20 subtiles first (Dragon Flight, Telekinesis have a range).
                            if(nearest&&std::getenv("D2MODERN_NEXT_APPROACH")&&(best>approachRange*approachRange||!clearLine(nearest->position)))approach(d2::subtileOf(nearest->position));
                            else if(nearest){pendingCommands.push_back(d2::Command::castOn(true,int32_t(nearest->id-1000)));++casts;}
                        }
                        runTick();
                        if(audio||std::getenv("D2MODERN_SEQUENCE_LOG"))refreshView(0);
                    }
                    std::cout<<(onCorpse?"Corpse skill ":"Next skill ")<<skillId<<": "<<casts<<" casts, corpses "<<corpsesBefore<<" -> "<<count(d2::EntityKind::Corpse)<<", units "<<unitsBefore<<" -> "<<worldView.entities.size()<<", ground items "<<groundBefore<<" -> "<<groundItems()<<", at most "<<mostMissiles<<" missiles, ends at tick "<<worldView.tick<<", player modes:";
                    for(int m:modes)std::cout<<" "<<m;
                    std::cout<<"\n";
                }
            }
            refreshView(0);
            if(walkGrid&&!std::getenv("D2MODERN_STOP_AT_MODE")&&!std::getenv("D2MODERN_STOP_MISSILE_AGE")){renderer.view.tick=captureTick;renderer.view.moving=captureWalking;renderer.view.facing=captureFacing;}
            if(std::getenv("D2MODERN_AUTOMAP"))automapOpen=true; // test hook: capture with the automap shown
            if(std::getenv("D2MODERN_OPEN_STASH")&&walkGrid) {
                // Test hook: walk to the town's stash (objects.txt row 267) and open it.
                for(const auto& o:worldView.objects)if(o.classId==267){pendingCommands.push_back(d2::Command::interactObject(int32_t(o.id)));break;}
                for(int t=0;t<1500&&!stashOpen;++t)runTick();
                for(int t=0;t<10;++t)runTick();
                std::cout<<(stashOpen?"Stash opened":"Stash did not open")<<"\n";
                refreshView(0);
            }
            if(std::getenv("D2MODERN_SHOW_CUBE")){cubeOpen=true;inventoryOpen=true;} // test hook: draw the cube panel (no cube needed)
            if(const char* list=std::getenv("D2MODERN_HOTKEYS")) {
                // Test hook: "id,id,..." the F1, F2, ... hotkey skills (the phone controls' skill buttons).
                std::istringstream ids(list);std::string id;size_t slot=0;
                while(slot<skillHotkeys.size()&&std::getline(ids,id,','))skillHotkeys[slot++]=std::atoi(id.c_str());
            }
            if(const char* skill=std::getenv("D2MODERN_RIGHT_SKILL")) {
                // Test hook: that skill on the right button (skills.txt row), as the skill picker would choose it.
                selectSkill(std::atoi(skill),false);
                for(int t=0;t<10;++t)runTick();
                refreshView(0);
            }
            if(const char* skill=std::getenv("D2MODERN_LEFT_SKILL")) {
                // Test hook: the same for the left button (what a plain attack uses).
                selectSkill(std::atoi(skill),true);
                for(int t=0;t<10;++t)runTick();
                refreshView(0);
            }
            if(const char* quests=std::getenv("D2MODERN_OPEN_QUESTS")) {
                // Test hook: opens the quest log (the value picks the act, 1-5) and waits for the game's answer.
                toggleQuestLog();
                for(int t=0;t<20;++t)runTick();
                if(std::atoi(quests)>=1&&std::atoi(quests)<=5)questLog.act=std::atoi(quests)-1;
                std::cout<<"Quest log: "<<(questLog.known?"received":"no answer");
                for(int q:{1,2,3,4,5,6})std::cout<<" q"<<q<<"=0x"<<std::hex<<questLog.flags[size_t(q)]<<std::dec<<"/"<<int(questLog.state[size_t(q)]);
                std::cout<<"\n";refreshView(0);
            }
            if(const char* death=std::getenv("D2MODERN_DEATH")) {
                // Test hook (with D2MODERN_TEST_DIE=N): "show" waits until the player lies dead; "respawn" then presses Esc and
                // waits until the player is alive again.
                int t=0;while(worldView.player().mode!=17&&t++<600)runTick();
                std::cout<<(worldView.player().mode==17?"Player lies dead":"Player did not die")<<" after "<<t<<" ticks\n";
                if(std::string(death)=="respawn") {
                    d2::InputEvent esc;esc.type=d2::InputEvent::Type::Key;esc.key=d2::key::Escape;handleInput(esc);
                    t=0;while(worldView.vitals.life<=0&&t++<600)runTick();
                    for(int i=0;i<30;++i)runTick();
                    std::cout<<(worldView.vitals.life>0?"Respawned":"Did not respawn")<<", life "<<worldView.vitals.life<<", level "<<worldView.level<<"\n";
                    for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Player&&e.id!=worldView.localPlayer)std::cout<<"Player body "<<((e.id-1000)&~d2::playerIdBit)<<" mode "<<int(e.mode)<<" at "<<e.position.x/d2::fixedOne<<","<<e.position.y/d2::fixedOne<<"\n";
                }
                refreshView(0);
            }
            if(const char* list=std::getenv("D2MODERN_CLICKS")) {
                // Test hook: "x,y x,y ..." window points clicked in turn once the scene is set up (after the hooks above). A point
                // starting with r is a right click, with s a left click with shift held. D2MODERN_ITEMS_LOG=1 prints the
                // player's items after each click.
                auto logItems=[&](const std::string& after){
                    if(!std::getenv("D2MODERN_ITEMS_LOG"))return;
                    std::cout<<"Items after "<<after<<":";
                    for(const auto& item:worldView.items)if(!item.vendor&&item.mode!=3&&item.mode!=5)
                        std::cout<<" "<<item.id<<":"<<d2::ItemTable::codeString(item.code)<<"/m"<<item.mode<<"/p"<<item.page<<"@"<<item.x<<","<<item.y<<(item.mode==1?"/b"+std::to_string(item.bodyLoc):"")<<"/q"<<item.quantity<<"/t"<<item.traits<<(item.sockets?"/s"+std::to_string(item.sockets):"")<<(item.parent?"/in"+std::to_string(item.parent):"")<<"/f"<<std::hex<<item.flags<<std::dec;
                    std::cout<<"\n";
                };
                logItems("setup");
                std::istringstream points(list);std::string point;
                while(points>>point) {
                    if(int x1=0,y1=0,x2=0,y2=0;!point.empty()&&point[0]=='d'&&std::sscanf(point.c_str()+1,"%d,%d:%d,%d",&x1,&y1,&x2,&y2)==4) {
                        // "dX1,Y1:X2,Y2": a drag, the left button down at the first point and up at the second 5 ticks later.
                        renderer.draw();
                        d2::InputEvent e;e.type=d2::InputEvent::Type::ButtonDown;e.button=0;e.x=x1;e.y=y1;mouseX=x1;mouseY=y1;handleInput(e);
                        for(int tick=0;tick<5;++tick)runTick();
                        refreshView(0);renderer.draw();if(std::getenv("D2MODERN_CURSOR_LOG"))std::cout<<"Dragging: cursor item "<<cursorItem<<", drag armed "<<itemDrag.armed<<std::endl;
                        e.type=d2::InputEvent::Type::MouseMove;e.x=x2;e.y=y2;handleInput(e);
                        e.type=d2::InputEvent::Type::ButtonUp;handleInput(e);
                        for(int tick=0;tick<15;++tick)runTick();
                        refreshView(0);logItems(point);
                        if(std::getenv("D2MODERN_CURSOR_LOG"))std::cout<<"After "<<point<<": cursor item "<<cursorItem<<", gold "<<worldView.vitals.gold<<std::endl;
                        continue;
                    }
                    unsigned button=0;scriptedShift=false;
                    if(!point.empty()&&point[0]=='r'){button=1;point.erase(0,1);}
                    else if(!point.empty()&&point[0]=='s'){scriptedShift=true;point.erase(0,1);}
                    if(!point.empty()&&point[0]=='w'&&point.size()>1){ // "wN": N milliseconds of real time pass, ticking (a build on another thread finishes)
                        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::atoi(point.c_str()+1));
                        long long longest=0;
                        while(std::chrono::steady_clock::now()<until){const auto step=std::chrono::steady_clock::now();runTick();refreshView(0);longest=std::max<long long>(longest,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-step).count());std::this_thread::sleep_for(std::chrono::milliseconds(20));}
                        std::cout<<"Waited "<<point.substr(1)<<" ms, longest step "<<longest<<" ms"<<std::endl;
                        continue;
                    }
                    if(int mx=0,my=0;!point.empty()&&point[0]=='m'&&std::sscanf(point.c_str()+1,"%d,%d",&mx,&my)==2){mouseX=mx;mouseY=my;renderer.draw();continue;} // "mX,Y": the mouse moved there only
                    if(!point.empty()&&point[0]=='k'&&point.size()>1){ // "kX": the key X pressed ("sk1": shift held), the frame drawn first
                        renderer.draw();d2::InputEvent e;e.type=d2::InputEvent::Type::Key;e.key=unsigned(std::toupper(static_cast<unsigned char>(point[1])));handleInput(e);scriptedShift=false;
                        for(int tick=0;tick<15;++tick)runTick();
                        refreshView(0);logItems(point);continue;
                    }
                    int x=0,y=0;if(std::sscanf(point.c_str(),"%d,%d",&x,&y)!=2)continue;
                    renderer.draw();
                    mouseX=x;mouseY=y;
                    scriptedClick(x,y,button);scriptedShift=false;
                    for(int tick=0;tick<15;++tick)runTick();
                    refreshView(0);
                    logItems(point);
                    if(std::getenv("D2MODERN_CURSOR_LOG"))std::cout<<"After "<<point<<": player "<<worldView.player().position.x/d2::fixedOne<<","<<worldView.player().position.y/d2::fixedOne<<", npc "<<(npcDialog.open?"open":"closed")<<", store button "<<storeCursor<<", selling "<<(sellToNpc!=0)<<", repairing "<<(repairAtNpc!=0)<<", dialog "<<dialogX<<","<<dialogY<<" "<<dialogW<<"x"<<dialogH<<std::endl;
                }
            }
            if(const char* script=std::getenv("D2MODERN_TOUCHES")) {
                // Test hook: fingers, as a phone would give them. Steps separated by ';': "d F X Y" (finger F down at window
                // point X,Y), "m F X Y" (moves), "u F" (lifts), "t N" (N game ticks pass). Prints where the player is
                // after each step that lets time pass.
                std::istringstream steps(script);std::string step;
                std::map<int64_t,std::pair<int,int>> at;
                while(std::getline(steps,step,';')) {
                    std::istringstream words(step);char op=0;words>>op;
                    if(op=='t') {
                        int ticks=0;words>>ticks;
                        for(int tick=0;tick<ticks;++tick){touch::tick();runTick();}
                        refreshView(0);
                        const auto p=d2::subtileOf(worldView.player().position);
                        std::cout<<"Touch test: after "<<ticks<<" ticks the player is at "<<p.x<<","<<p.y;
                        if(const uint32_t aimed=touch::target()) {
                            for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(aimed)) {
                                const auto q=d2::subtileOf(e.position);
                                std::cout<<"; aiming at enemy "<<aimed<<" (class "<<e.classId<<") at "<<q.x<<","<<q.y<<", life "<<e.life<<"/"<<e.maxLife;
                            }
                        }
                        std::cout<<"; right skill "<<worldView.rightSkill<<", mana "<<worldView.stats[8]/256<<"/"<<worldView.stats[9]/256;
                        std::cout<<"; belt "<<beltItem(0)<<","<<beltItem(1)<<(npcDialog.open?"; talking":"")<<(waypointMenu.open?"; waypoints open":"");
                        std::cout<<"; ground items "<<std::count_if(worldView.items.begin(),worldView.items.end(),[](const d2::ItemSnapshot& i){return i.mode==3||i.mode==5;});
                        std::cout<<"; backpack "<<std::count_if(worldView.items.begin(),worldView.items.end(),[](const d2::ItemSnapshot& i){return i.mode==0&&i.page==0&&!i.vendor;});
                        std::cout<<"; enemies alive within reach:";
                        int alive=0;for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy&&e.life!=0){const auto q=d2::subtileOf(e.position);if(std::abs(q.x-p.x)<=18&&std::abs(q.y-p.y)<=18)++alive;}
                        std::cout<<" "<<alive<<"\n";
                        continue;
                    }
                    if(op=='p'||op=='a') {
                        // A game controller: "p BUTTON 1|0" (pad:: numbers) presses or lets go, "a AXIS VALUE" moves an axis.
                        d2::InputEvent pad;pad.type=op=='p'?d2::InputEvent::Type::PadButton:d2::InputEvent::Type::PadAxis;
                        int pressed=0;words>>pad.button;if(op=='p'){words>>pressed;pad.pressed=pressed!=0;}else words>>pad.value;
                        renderer.draw();
                        handleInput(pad);
                        continue;
                    }
                    if(op=='f') {
                        // "f N": N frames of 20 ms (the controller's cursor moves by the frame).
                        int frames=0;words>>frames;
                        for(int i=0;i<frames;++i){std::this_thread::sleep_for(std::chrono::milliseconds(20));touch::update();}
                        std::cout<<"Touch test: cursor at "<<mouseX<<","<<mouseY<<(inventoryOpen?"; inventory open":"")<<(cursorItem?"; item on the cursor":"")<<"\n";
                        continue;
                    }
                    d2::InputEvent e;int64_t finger=0;words>>finger;e.finger=finger;
                    if(op=='d'||op=='m'){words>>e.x>>e.y;at[finger]={e.x,e.y};e.type=op=='d'?d2::InputEvent::Type::TouchDown:d2::InputEvent::Type::TouchMove;}
                    else if(op=='u'){e.x=at[finger].first;e.y=at[finger].second;e.type=d2::InputEvent::Type::TouchUp;}
                    else continue;
                    renderer.draw();
                    handleInput(e);
                }
            }
            if(const char* walkTo=std::getenv("D2MODERN_WALK_TO");walkTo&&walkGrid) {
                // Test hook: D2MODERN_WALK_TO=dx,dy walks the player that many subtiles from where it stands, then captures.
                int dx=0,dy=0;
                if(std::string(walkTo)=="tile") {
                    // D2MODERN_WALK_TO=tile: next to the nearest level entrance (stairs, cave mouth, door).
                    const auto& p=worldView.player().position;
                    for(const auto& t:worldView.tiles)std::cout<<"Entrance "<<t.classId<<" at "<<t.x<<","<<t.y<<" (player "<<p.x/d2::fixedOne<<","<<p.y/d2::fixedOne<<")\n";
                    if(!worldView.tiles.empty()){dx=worldView.tiles.front().x+3-int32_t(p.x/d2::fixedOne);dy=worldView.tiles.front().y+3-int32_t(p.y/d2::fixedOne);}
                    walkTo="";
                }
                if(!*walkTo||std::sscanf(walkTo,"%d,%d",&dx,&dy)==2) {
                    const auto& p=worldView.player().position;
                    pendingCommands.push_back(d2::Command::moveTo(d2::GridPoint{int32_t(p.x/d2::fixedOne)+dx,int32_t(p.y/d2::fixedOne)+dy}));
                    std::string facings;int lastFacing=-1; // the facings the walk went through (printed with it)
                    int ticks=0;do{runTick();refreshView(0);++ticks;if(worldView.player().moving&&worldView.player().facing!=lastFacing){lastFacing=worldView.player().facing;facings+=" "+std::to_string(lastFacing);}}while((ticks<30||worldView.player().moving)&&ticks<2000); // far walks start on the tenth tick
                    std::cout<<"Facings:"<<facings<<"\n";
                    const auto& q=worldView.player().position;
                    std::cout<<"Walked to "<<q.x/d2::fixedOne<<","<<q.y/d2::fixedOne<<" in "<<ticks<<" ticks (level "<<worldView.level<<")\n";                    refreshView(0);
                }
            }
            if(const char* lanTicks=std::getenv("D2MODERN_LAN_TICKS");lanTicks&&walkGrid&&!nativeSave.empty()) {
                // Test hook for LAN play: keep the game going this many ticks at the real pace (a guest can join the host
                // meanwhile), then report the other players this window sees.
                // Another player in a skill's sequence (Charge, Leap...) is logged as it is drawn from; with
                // D2MODERN_LAN_STOP_SEQUENCE=N the run stops N ticks into the first one, for the capture.
                const int ticks=std::atoi(lanTicks);size_t mostPlayers=0;
                const char* stopSequence=std::getenv("D2MODERN_LAN_STOP_SEQUENCE");int sequenceTicks=0;
                for(int t=0;t<ticks;++t) {
                    runTick();refreshView(0);
                    size_t players=0;for(const auto& e:worldView.entities)players+=e.kind==d2::EntityKind::Player&&e.id!=worldView.localPlayer;
                    mostPlayers=std::max(mostPlayers,players);
                    bool inSequence=false;
                    for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Player&&e.id!=worldView.localPlayer&&(e.mode==18||e.skillMoveFrame!=255)) {
                        inSequence=true;
                        std::cout<<"LAN: other player tick "<<worldView.tick<<" skill "<<e.usedSkill<<" mode "<<int(e.mode)<<" table "<<int(e.sequenceMode)<<"/"<<int(e.sequenceFrame)
                            <<" leap frame "<<int(e.skillMoveFrame)<<" at "<<e.position.x/d2::fixedOne<<","<<e.position.y/d2::fixedOne<<"\n";
                    }
                    if(stopSequence&&inSequence&&++sequenceTicks>=std::atoi(stopSequence))break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Player&&e.id!=worldView.localPlayer)
                    std::cout<<"LAN: another player, class "<<e.classId<<", at "<<e.position.x/d2::fixedOne<<","<<e.position.y/d2::fixedOne<<", mode "<<int(e.mode)<<", life "<<e.life<<"/"<<e.maxLife<<"\n";
                const auto& me=worldView.player().position;
                std::cout<<"LAN: after "<<ticks<<" ticks I am at "<<me.x/d2::fixedOne<<","<<me.y/d2::fixedOne<<" (level "<<worldView.level<<"), most other players seen "<<mostPlayers<<"\n";
            }
            if(const char* filler=std::getenv("D2MODERN_TEST_SOCKET");filler&&*filler) {
                // Test hook: put a carried item of this code (a gem, rune, jewel; given with D2MODERN_TEST_GIVE) into the first
                // carried item with a free socket: pick it up (0x19), then 0x28 onto the target, as a click with it does.
                int32_t gem=0,target=0;
                for(const auto& item:worldView.items)if(!item.vendor&&item.mode==0) {
                    if(!gem&&d2::ItemTable::codeString(item.code)==filler&&(item.traits&d2::native::itemTraitSocketFiller))gem=int32_t(item.id);
                    if(!target&&(item.traits&d2::native::itemTraitTakesFiller))target=int32_t(item.id);
                }
                std::cout<<"Socketing: filler "<<gem<<" into item "<<target<<"\n";
                if(gem&&target) {
                    pendingCommands.push_back(d2::Command::raw(0x19|(5<<8),gem,0,0,0));for(int t=0;t<6;++t)runTick();
                    pendingCommands.push_back(d2::Command::raw(0x28|(9<<8),gem,target,0,0));for(int t=0;t<6;++t)runTick();
                }
            }
            if(const char* cycle=std::getenv("D2MODERN_TIME_OF_DAY");cycle&&*cycle) {
                // Test hook: the act's time of day to this cycle (0 sunrise, 1 morning, 2 noon, 3 afternoon, 4 sunset, 5 night).
                pendingCommands.push_back(d2::Command::cheat(8,std::atoi(cycle)));
                for(int t=0;t<3;++t)runTick();
            }
            if(const char* phase=std::getenv("D2MODERN_WEATHER");phase&&*phase) {
                // Test hook: the weather straight to this phase (2 the full storm; Weather.h), on a level that has weather.
                gameWeather.forcePhase(std::atoi(phase));
                for(int t=0;t<40;++t){runTick();refreshView(1.f);}
            }
            if(const char* ticks=std::getenv("D2MODERN_FACING_LOG")) {
                // Test hook: this many ticks pass; for each monster or town person that walked, how many walking ticks it had and
                // how often its facing changed meanwhile (a zigzag shows as many changes).
                std::map<d2::EntityId,std::pair<int,int>> counts;std::map<d2::EntityId,uint8_t> last;
                for(int t=0;t<std::atoi(ticks);++t) {
                    runTick();refreshView(1.f);
                    for(const auto& e:worldView.entities)if((e.kind==d2::EntityKind::Enemy||e.kind==d2::EntityKind::TownPerson)&&e.moving) {
                        auto& c=counts[e.id];++c.first;
                        if(auto it=last.find(e.id);it!=last.end()&&it->second!=e.facing)++c.second;
                        last[e.id]=e.facing;
                    }
                }
                for(const auto& [id,c]:counts)std::cout<<"Facing: unit "<<id<<" walked "<<c.first<<" ticks, turned "<<c.second<<" times\n";
            }
            if(const char* ticks=std::getenv("D2MODERN_AMBIENCE_TICKS")) {
                // Test hook: run this many ticks with the audio following them (music, ambience, events; see D2MODERN_SOUND_LOG).
                for(int t=0;t<std::atoi(ticks);++t){runTick();refreshView(1.f);}
            }
            if(const char* recipe=std::getenv("D2MODERN_TEST_CUBE");recipe&&*recipe) {
                // Test hook: open the Horadric Cube (a carried "box", given with D2MODERN_TEST_GIVE), put these carried items into
                // it one by one (0x19 take, 0x18 onto page 3), press Transmute (0x4F button 24) and list what the cube then holds.
                auto wait=[&](int n){for(int t=0;t<n;++t)runTick();};
                auto cubeItems=[&]{std::string s;for(const auto& item:worldView.items)if(!item.vendor&&item.mode==0&&item.page==3)s+=" "+d2::ItemTable::codeString(item.code)+"#"+std::to_string(item.id)+"/q"+std::to_string(item.quality);return s;};
                int32_t cube=0;
                for(const auto& item:worldView.items)if(!item.vendor&&item.mode==0&&d2::ItemTable::codeString(item.code)=="box")cube=int32_t(item.id);
                if(cube){pendingCommands.push_back(d2::Command::useItem(cube));wait(10);}
                std::cout<<"Cube: item "<<cube<<", open "<<cubeOpen<<"\n";
                std::istringstream codes(recipe);std::string code;int32_t x=0;std::set<uint32_t> used;
                while(codes>>code) {
                    int32_t id=0;
                    for(const auto& item:worldView.items)if(!item.vendor&&item.mode==0&&item.page!=3&&!used.count(item.id)&&d2::ItemTable::codeString(item.code)==code){id=int32_t(item.id);break;}
                    if(!id){std::cout<<"Cube: no carried "<<code<<"\n";continue;}
                    used.insert(uint32_t(id));
                    pendingCommands.push_back(d2::Command::raw(0x19|(5<<8),id,0,0,0));wait(6);
                    pendingCommands.push_back(d2::Command::placeItem(id,x,0,3));wait(6);
                    x+=1;
                }
                std::cout<<"Cube before:"<<cubeItems()<<"\n";
                pendingCommands.push_back(d2::Command::raw(0x4F|(7<<8),24,0,0,0));wait(15);
                std::cout<<"Cube after:"<<cubeItems()<<"\n";
            }
            if(std::getenv("D2MODERN_ITEM_STATS_LOG")) {
                // Test hook: each carried item's quality and magic properties (stat:param=value), as the host sends them.
                for(const auto& item:worldView.items)if(!item.vendor&&item.mode!=3&&item.mode!=5) {
                    std::cout<<"Item "<<item.id<<" "<<d2::ItemTable::codeString(item.code)<<" quality "<<item.quality<<(item.sockets?" sockets "+std::to_string(item.sockets):std::string())<<(item.parent?" in "+std::to_string(item.parent):std::string())<<(item.invTrans?" colour "+std::to_string(item.invTrans)+"/"+std::to_string(item.invColor)+" worn "+std::to_string(item.chrTrans)+"/"+std::to_string(item.chrColor):std::string())<<":";
                    for(const auto& s:item.stats)std::cout<<" "<<s.id<<":"<<s.param<<"="<<s.value;
                    if(!item.setStats.empty()){std::cout<<" set";for(const auto& s:item.setStats)std::cout<<" "<<s.id<<":"<<s.param<<"="<<s.value;}
                    std::cout<<"\n";
                    if(itemStats&&strings)for(const auto& line:d2::describeItemStats(item.stats,*itemStats,*strings,skillTable.get(),worldView.vitals.level))std::cout<<"    | "<<line<<"\n";
                    if(itemStats&&strings)for(const auto& line:d2::describeItemStats(item.setStats,*itemStats,*strings,skillTable.get(),worldView.vitals.level))std::cout<<"    set | "<<line<<"\n";
                }
            }
            if(std::getenv("D2MODERN_LIST_MONSTERS")) {
                // Test hook: the champions, uniques and minions the game knows about, with their map cell (for D2MODERN_LOOK_AT).
                const bool all=std::string(std::getenv("D2MODERN_LIST_MONSTERS"))=="all"; // "all": every enemy
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy&&(e.monsterType||all))
                    std::cout<<"Monster "<<e.classId<<" type 0x"<<std::hex<<int(e.monsterType)<<std::dec<<" colour "<<int(e.colorTransform)
                        <<" cell "<<e.position.x/d2::fixedOne/5<<","<<e.position.y/d2::fixedOne/5<<"\n";
            }
            if(std::getenv("D2MODERN_ATTACK_TEST")&&walkScene) {
                // Test hook: attack the nearest enemy; while the player walks up to it, print its facing next to the enemy's
                // direction (0-15), which should agree.
                const d2::EntitySnapshot* target=nullptr;double best=1e30;
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy) {
                    const double dx=double(e.position.x-worldView.player().position.x),dy=double(e.position.y-worldView.player().position.y);
                    if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;target=&e;}
                }
                if(target) {
                    const auto guid=int32_t(target->id-1000);
                    pendingCommands.push_back(d2::Command::attack(guid));
                    std::string log;
                    const int ticks=std::max(60,std::atoi(std::getenv("D2MODERN_ATTACK_TEST"))); // the value: how long to keep at it
                    for(int t=0;t<ticks;++t) {
                        runTick();refreshView(0);
                        for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(guid)&&worldView.player().moving) {
                            const double deg=std::atan2(double(e.position.y-worldView.player().position.y),double(e.position.x-worldView.player().position.x))*180/3.14159265358979;
                            const int toward=((int(std::lround((deg-45)/22.5))%16)+16)%16;
                            log+=" "+std::to_string(worldView.player().facing)+"/"+std::to_string(toward);
                        }
                    }
                    std::cout<<"Attack approach (facing/enemy):"<<log<<"\nAttack test ends at tick "<<worldView.tick<<"\n";
                }
            }
            if(const char* look=std::getenv("D2MODERN_LOOK_AT");look&&walkScene) {
                // Test hook: D2MODERN_LOOK_AT=cellX,cellY puts that map cell (scene cells) in the middle of the capture;
                // "target" puts the unit the player last used a skill on there (a curse's or a spell's effects).
                int cx=0,cy=0;
                if(std::string(look)=="target")for(const auto& e:worldView.entities)if(e.id==d2::EntityId(1000+actionTargetUnit)) {
                    followPlayer=false;
                    const auto p=d2::projectPoint(*walkScene,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne);
                    panX=-(p[0]-walkScene->width*.5f)*zoom;panY=-(p[1]-walkScene->height*.5f)*zoom;
                }
                if(std::sscanf(look,"%d,%d",&cx,&cy)==2) {
                    followPlayer=false;
                    const auto p=d2::projectPoint(*walkScene,cx*5.f+2.5f,cy*5.f+2.5f);
                    panX=-(p[0]-walkScene->width*.5f)*zoom;panY=-(p[1]-walkScene->height*.5f)*zoom;
                }
            }
            if(std::getenv("D2MODERN_PICK_LOG")&&gameUnitArt) {
                // Test hook: each kind of monster or NPC in view, and whether the mouse can select it (monstats2.txt isSel, not noSel).
                std::set<uint32_t> seen;
                for(const auto& e:worldView.entities)if((e.kind==d2::EntityKind::TownPerson||e.kind==d2::EntityKind::Enemy)&&seen.insert(e.classId).second) {
                    const auto x=e.position.x/d2::fixedOne,y=e.position.y/d2::fixedOne;
                    if(const auto* s=gameUnitArt->sprite(e.classId))std::cout<<"Pick: "<<s->id<<" ("<<e.classId<<") "<<(s->selectable?"selectable":"not selectable")<<" at "<<x<<","<<y<<"\n";
                    else std::cout<<"Pick: class "<<e.classId<<" kind "<<int(e.kind)<<" has no monstats row, at "<<x<<","<<y<<"\n";
                }
            }
            if(std::getenv("D2MODERN_HOVER_ENEMY")&&walkScene) {
                renderer.draw(); // places the camera
                // Test hook for captures: the cursor rests on the visible enemy nearest the middle of the window (its name and life
                // show at the top).
                const auto r=clientRect();float best=1e30f;mouseX=int(r.right*.6f);mouseY=int(r.bottom*.4f); // no enemy in sight: an empty spot
                for(const auto& e:worldView.entities)if(e.kind==d2::EntityKind::Enemy) {
                    const auto a=d2::projectPoint(*walkScene,float(e.position.x)/d2::fixedOne,float(e.position.y)/d2::fixedOne);
                    const float x=r.right*.5f+(a[0]-walkScene->width*.5f)*zoom+panX,y=r.bottom*.5f+(a[1]-30-walkScene->height*.5f)*zoom+panY;
                    const float d=std::hypot(x-r.right*.5f,y-r.bottom*.5f);
                    if(x>=0&&y>=0&&x<r.right&&y<r.bottom&&d<best){best=d;mouseX=int(x);mouseY=int(y);}
                }
            }
            if(const char* hoverItem=std::getenv("D2MODERN_HOVER_ITEM")) {
                // Test hook for captures: with the inventory open, the cursor rests on the Nth carried item (from 1) that has magic
                // properties, so its tooltip shows them.
                renderer.draw(); // lays out the inventory's item regions
                // "#id" names the item itself (a socketed item whose properties are all in its fillers).
                const bool byId=hoverItem[0]==35;const int32_t id=byId?std::atoi(hoverItem+1):0;
                int wanted=std::max(1,std::atoi(hoverItem));
                if(byId)for(const auto& region:hirelingRegions)if(region.kind==80&&region.item==id){mouseX=region.x+region.w/2;mouseY=region.y+region.h/2;} // or on the hireling
                for(const auto& region:inventoryRegions)if(region.item)
                    for(const auto& item:worldView.items)if(int32_t(item.id)==region.item&&(byId?int32_t(item.id)==id:(!item.stats.empty()&&--wanted==0)))
                        {mouseX=region.x+region.w/2;mouseY=region.y+region.h/2;}
            }
            if(std::getenv("D2MODERN_AFFIX_LOG")&&gameItems) {
                // Test hook: how the client names the magic and rare items (to compare with the game's "Affixes:" lines).
                for(const auto& item:worldView.items)if(item.quality==4||item.quality==6)
                    std::cout<<"Client name: item "<<item.id<<" quality "<<item.quality<<" prefix "<<item.prefix<<" suffix "<<item.suffix<<" rare "<<item.rarePrefix<<","<<item.rareSuffix
                        <<" -> '"<<gameItems->displayName(item.code,item.quality,item.prefix,item.suffix,item.rarePrefix,item.rareSuffix,item.fileIndex)<<"'\n";
            }
            if(std::getenv("D2MODERN_LIST_OBJECTS")&&walkScene) {
                // Test hook: the game objects within 30 subtiles of the player, nearest first.
                const auto p=d2::subtileOf(worldView.player().position);
                std::vector<const d2::ObjectSnapshot*> near;
                for(const auto& o:worldView.objects)if(std::abs(o.x-p.x)<=30&&std::abs(o.y-p.y)<=30)near.push_back(&o);
                std::sort(near.begin(),near.end(),[&](auto* a,auto* b){return std::hypot(a->x-p.x,a->y-p.y)<std::hypot(b->x-p.x,b->y-p.y);});
                std::cout<<"Objects near the player at "<<p.x<<","<<p.y<<" (level "<<worldView.level<<"):\n";
                for(const auto* o:near) {
                    const auto* info=objectTable?objectTable->find(o->classId):nullptr;
                    std::cout<<"  object "<<o->id<<" class "<<o->classId<<" '"<<(info?info->name:"?")<<"' token "<<(info?info->token:"?")<<" mode "<<o->mode
                        <<" at "<<o->x<<","<<o->y<<" ("<<(o->x-p.x)<<","<<(o->y-p.y)<<") flags "<<o->flags<<(info&&o->mode<8&&info->selectable[o->mode]?" selectable":"")<<"\n";
                }
            }
            if(const char* hover=std::getenv("D2MODERN_HOVER_OBJECT");hover&&walkScene) {
                // Test hook: the cursor on that object (objects.txt row, e.g. 267 the stash); two frames find it and light it up.
                renderer.draw();
                const auto r=clientRect();
                for(const auto& o:worldView.objects)if(o.classId==uint32_t(std::atoi(hover)))for(const auto& box:objectPicks)if(box.guid==o.id) {
                    mouseX=int(r.right*.5f+(box.x+box.w*.5f-walkScene->width*.5f)*zoom+panX);mouseY=int(r.bottom*.5f+(box.y+box.h*.5f-walkScene->height*.5f)*zoom+panY);
                }
                renderer.draw();
                std::cout<<"Hovered object: "<<hoveredObject<<"\n";
            }
            if(const char* held=std::getenv("D2MODERN_WAYPOINT_CLICK_TEST");held&&walkScene&&objectTable) {
                // Test hook: from spots around the waypoint, press the left button on it, hold it this many ticks (walking on
                // while held, as the game loop does), let go, and report whether the waypoint menu opened.
                const int holdFor=std::atoi(held);int opened=0,tries=0;
                const d2::ObjectSnapshot* waypoint=nullptr;
                for(const auto& o:worldView.objects)if(const auto* info=objectTable->find(o.classId);info&&info->name=="Waypoint")waypoint=&o;
                if(!waypoint)throw std::runtime_error("D2MODERN_WAYPOINT_CLICK_TEST: no waypoint nearby");
                const uint32_t guid=waypoint->id;const d2::GridPoint at{waypoint->x,waypoint->y};
                for(const auto& offset:std::vector<std::pair<int,int>>{{-12,0},{12,0},{0,-12},{0,12},{-9,9},{9,-9},{-6,-6},{8,8}}) {
                    pendingCommands.push_back(d2::Command::moveTo(d2::GridPoint{at.x+offset.first,at.y+offset.second}));
                    for(int t=0;t<30||(worldView.player().moving&&t<400);++t){runTick();refreshView(0);}
                    renderer.draw();renderer.draw(); // the camera, then the boxes where the waypoint is drawn now
                    const auto r=clientRect();bool found=false;
                    for(const auto& box:objectPicks)if(box.guid==guid) {
                        mouseX=int(r.right*.5f+(box.x+box.w*.5f-walkScene->width*.5f)*zoom+panX);mouseY=int(r.bottom*.5f+(box.y+box.h*.5f-walkScene->height*.5f)*zoom+panY);found=true;
                    }
                    if(!found){std::cout<<"Waypoint click from "<<offset.first<<","<<offset.second<<": waypoint not on screen\n";continue;}
                    d2::InputEvent e;e.type=d2::InputEvent::Type::ButtonDown;e.button=0;e.x=mouseX;e.y=mouseY;handleInput(e);
                    for(int t=0;t<holdFor;++t){holdWalk();runTick();refreshView(0);}
                    e.type=d2::InputEvent::Type::ButtonUp;handleInput(e);
                    for(int t=0;t<300&&!waypointMenu.open;++t){runTick();refreshView(0);}
                    ++tries;opened+=waypointMenu.open;
                    const auto p=d2::subtileOf(worldView.player().position);
                    std::cout<<"Waypoint click from "<<offset.first<<","<<offset.second<<": "<<(waypointMenu.open?"menu opened":"NO MENU")<<" (player at "<<p.x-at.x<<","<<p.y-at.y<<" from it)\n";
                    if(waypointMenu.open)closeWaypointMenu();
                    for(int t=0;t<10;++t)runTick();
                }
                std::cout<<"Waypoint clicks: "<<opened<<"/"<<tries<<" opened the menu (held "<<holdFor<<" ticks)\n";
            }
            if(std::getenv("D2MODERN_WEATHER"))for(int t=0;t<12;++t){runTick();refreshView(1.f);renderer.draw();std::this_thread::sleep_for(std::chrono::milliseconds(45));} // test hook: frames drawn first, so rain splashes are under way
            if(!renderer.draw()) throw std::runtime_error("Smoke-test window has no drawable area");
            if(const char* level=std::getenv("D2MODERN_BRIGHTNESS")){touch::settings().brightness=std::clamp(std::atoi(level),0,10);renderer.drawBrightness();} // test hook: the brightness setting in the capture
            if(std::getenv("D2MODERN_HOVER_ENEMY")||std::getenv("D2MODERN_HOVER_OBJECT")||std::getenv("D2MODERN_CURSOR_LOG"))renderer.drawCursor(); // the capture shows where the cursor is
            const size_t pixels=renderer.readback(capture);
            if(!pixels) throw std::runtime_error("Render readback contains only background");
            std::cout<<"Render verified: "<<pixels<<" non-background pixels; "<<(renderer.software()?"WARP":"hardware")<<"\n";
            if(map&&!referenceMap)std::cout<<"Visible tiles: "<<renderer.visibleCount<<"/"<<scene.placements.size()<<"\n";
            return 0;
        }

        if(walkGrid&&!noAudio){if(!audio)audio=std::make_unique<d2::Audio>(archives);applyAudioSettings();gameAudio=audio.get();
            // The Act I tracks the player hears first are read in the background now, so leaving town doesn't wait for them.
            for(const char* track:{"town1","wild"})audio->preload(std::string("data\\global\\music\\Act1\\")+track+".wav");soundMonsters=std::make_unique<d2::MonsterTable>(archives);soundBank=std::make_unique<d2::SoundBank>(archives,*soundMonsters);gameSounds=soundBank.get();}
        using Clock=std::chrono::steady_clock;
        constexpr std::chrono::nanoseconds tickLength{1'000'000'000/d2::simulationHz};
        constexpr std::chrono::nanoseconds maxCatchUp{250'000'000};
        WEB_STATIC bool running=true; WEB_STATIC uint32_t frame=0;
        WEB_STATIC std::chrono::steady_clock::time_point lastFrame; lastFrame=Clock::now();
        WEB_STATIC std::chrono::nanoseconds accumulator{};
        WEB_STATIC d2::GridPoint heldDirection{};
        // Run on or off as the player last left it (settings.ini "alwaysrun", the original's "Always Run" registry value),
        // read as the game starts the way D2Client 1.10f reads it (0x6FB08D90).
        if(world&&touch::settings().alwaysRun){runEnabled=true;pendingCommands.push_back(d2::Command::setRun(true));}
        // A TCP/IP game, hosted or joined: it never pauses.
        auto environmentSet=[](const char* name){const char* value=std::getenv(name);return value&&*value;};
        WEB_STATIC bool networkGame=false;
        networkGame=world&&(environmentSet("D2MODERN_LAN_HOST")||environmentSet("D2MODERN_LAN_JOIN"));
        // Its Wi-Fi kept out of power saving while it lasts (Android; see Window::lowLatencyNetwork).
        struct LowLatency {d2::Window* window;bool on;~LowLatency(){if(on)window->lowLatencyNetwork(false);}} lowLatency{window.get(),networkGame};
        if(networkGame)window->lowLatencyNetwork(true);
        WEB_STATIC std::function<void()> gameFrame;
        gameFrame=[&] {
        while(running) {
            window->setTextInput(goldPopup.open); // the amount of gold
            touch::update(); // a game controller's cursor in the panels
            for(d2::InputEvent e;window->pollEvent(e);) {
                if(e.type==d2::InputEvent::Type::Quit) { running=false; break; }
                if(leaveToMainMenu)break;
                handleInput(e);
            }
            if(!running||leaveToMainMenu) break; // closed, or Back to Main Menu (gameMain starts the menus again)
            if(walkGrid) {
                // Fixed-step simulation: run whole 25 Hz ticks, render the remainder interpolated.
                // Time does not accumulate while unfocused, so the game pauses in the background. A TCP/IP game never
                // pauses (the others play on): its time runs in the background and behind the game menu too.
                const auto now=Clock::now();
                const bool playing=
#ifdef __EMSCRIPTEN__
                    gameMenuPage==GameMenuPage::Closed;
#else
                    window->focused()&&gameMenuPage==GameMenuPage::Closed;
#endif
                if(playing||networkGame) {
                    accumulator+=std::min<std::chrono::nanoseconds>(now-lastFrame,maxCatchUp);
                    // Walking with W, A, S and D while the game has the keyboard (let go when it loses it).
                    d2::GridPoint direction{};
                    if(playing) {
                        auto down=[&](int key){return window->keyDown(unsigned(key));};
                        if(down('W')){--direction.x;--direction.y;}
                        if(down('S')){++direction.x;++direction.y;}
                        if(down('A')){--direction.x;++direction.y;}
                        if(down('D')){++direction.x;--direction.y;}
                        direction.x=std::clamp(direction.x,-1,1);direction.y=std::clamp(direction.y,-1,1);
                    }
                    if(!(direction==heldDirection)) {
                        heldDirection=direction;
                        pendingCommands.push_back(d2::Command::hold(direction.x,direction.y));
                        if(direction.x||direction.y)followPlayer=true;
                    }
                    try {
                        while(accumulator>=tickLength){holdWalk();touch::tick();runTick();accumulator-=tickLength;}
                    } catch(const d2::ConnectionLost& e) {
                        // The TCP/IP game's host is gone: back to the TCP/IP screen with why.
                        std::cout<<"TCP/IP game ended: "<<e.what()<<std::endl;
                        lanFailure=std::string(e.what());leaveToMainMenu=true;break;
                    }
                }
                lastFrame=now;
                refreshView(float(double(accumulator.count())/double(tickLength.count())));
            }
            if(frameStep) {
                if(count==1){frameStep=0;continue;}
                frame=uint32_t((int64_t(frame)+frameStep%int(count)+count)%count); frameStep=0;
                renderer.show(decode(frame)); setTitle(frame);
            }
            if(renderer.draw()) {
#ifndef __EMSCRIPTEN__
                if(map)setTitle(0);
#endif
                saveRequestedShot([&](const std::filesystem::path& p){renderer.readback(p);});renderer.present();yieldFrame(0);
            } else yieldFrame(50);
#ifdef __EMSCRIPTEN__
            break; // one frame; the browser calls us again
#endif
        }
        };
#ifdef __EMSCRIPTEN__
        static std::function<void()> persistGame;
        persistGame=gameFrame; // captures WEB_STATIC / global addresses
        gWebFrame=[] {
            persistGame();
            if(!running||leaveToMainMenu) {
                emscripten_cancel_main_loop();
                if(leaveToMainMenu) {
                    // As gameMain does on the desktop: the game is closed (D2Game saves the character), the client's state is
                    // reset and the menus start again.
                    world.reset();
                    resetClientState();
                    backFromGame=true;
                    std::cout<<"Back to the main menu"<<std::endl;
                    webRun();
                } else webGameEnded();
            }
        };
        emscripten_set_main_loop(webFrameTrampoline,0,1);
        return 0;
#else
        gameFrame();
        return 0;
#endif
    } catch(const std::exception& e) { std::cerr<<"D2Next: "<<e.what()<<"\n"; d2::reportFatal(e.what()); return 1; }
}
// The game libraries keep their state for the whole program (D2MOO, when linked in) and are not taken down: the program
// ends without global destructors, once the player has left the game (which saves the character).
// Back to Main Menu ends run() with leaveToMainMenu set: the game is closed (D2Game saves the character as it leaves), the
// client's state is reset and run() starts again at the main menu, in the same window.
int gameMain(int argc,wchar_t** argv) {
    // Test hook: D2MODERN_MAIN_MENU_TEST=N leaves the game N times (each test run goes straight back into it).
    int testReturns=std::getenv("D2MODERN_MAIN_MENU_TEST")?std::atoi(std::getenv("D2MODERN_MAIN_MENU_TEST")):0;
    for(;;) {
        const int code=run(argc,argv);
        const bool again=leaveToMainMenu||(code==0&&testReturns-->0);
        world.reset();
        if(!again)d2::endProgram(code);
        resetClientState();
        backFromGame=true;
        std::cout<<"Back to the main menu"<<std::endl;
    }
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {return gameMain(argc,argv);}
#elif !defined(__ANDROID__) // Android starts in PlatformAndroid.cpp
// Arguments come as UTF-8.
int main(int argc,char** argv) {
    std::vector<std::wstring> wide;
    for(int i=0;i<argc;++i)wide.push_back(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[i])).wstring());
    std::vector<wchar_t*> pointers;
    for(auto& w:wide)pointers.push_back(w.data());
    pointers.push_back(nullptr);
    return gameMain(argc,pointers.data());
}
#endif
