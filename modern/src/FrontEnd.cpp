#include "FrontEnd.h"
#include "Tables.h"
#include "Video.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include "Platform.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
namespace d2 {
namespace {
// Button types: 0 wide (widebuttonblank, 272x35), 1 medium (mediumbuttonblank, 128x35), 2 tall (tallbuttonblank, 168x60),
// 3 small (okcancelbuttonblank, 96x32), 4 popup (cancelbuttonblank, 96x32: the TCP/IP screen's popups).
constexpr int Wide=0,Medium=1,Tall=2,Small=3,Popup=4;
enum Action {SinglePlayer=1,BattleNet,OtherMultiplayer,Credits,Cinematics,ExitGame,CreateCharacter,DeleteCharacter,ExitSelect,Ok,ExitCreate,OkCreate,
    ConfirmDelete,CancelDelete,OpenBattleNet,TcpIpGame,CancelMultiplayer,HostGame,JoinGame,CancelTcpIp,AddressOk,AddressCancel,MessageOk,BackToMainMenu,
    PlayIntro,PlayAct1,PlayAct2,PlayAct3,PlayAct4,PlayAct5,PlayNormal,PlayNightmare,PlayHell,CancelDifficulty};
// The TCP/IP screens as D2Launch.dll (1.10f) lays them out (its control tables at 6FA36670-6FA369F0; there a control's
// y is its bottom edge, here the top): OTHER MULTIPLAYER's buttons, the TCP/IP screen's HOST GAME and JOIN GAME, the
// texts above them and the description shown while one is under the mouse, and the address popup.
constexpr float hostX=265,hostY=172,joinY=230,descriptionY=311,descriptionWidth=272;
constexpr float addressPopupX=268,addressPopupY=175,addressBoxX=291,addressBoxY=245,addressTextX=300;
// The rows of charselectdifficultylevels at (237,200): each bar 326 wide and 35 tall, from these tops.
constexpr float difficultyX=237,difficultyRowY[3]={262,306,348},difficultyRowH=35,difficultyW=326;
constexpr float slotX=37,slotY=86,slotW=272,slotH=95;
constexpr float saveScrollX=592,saveScrollY=86,saveScrollW=12,saveScrollH=380,saveScrollButtonH=14,saveScrollThumbH=14;
constexpr float movieBarX=150,movieBarY=552,movieBarW=500,movieBarH=8;
constexpr std::array<uint8_t,3> buttonColor{100,100,100},disabledColor{48,48,48},gold{199,179,119},white{255,255,255},green{24,255,0},lightBrown{188,168,140},red{255,77,77};
uint32_t read32(const Bytes& b,size_t o){return b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(uint32_t(b[o+3])<<24);}
Image solid(uint32_t w,uint32_t h,std::array<uint8_t,4> color) {
    Image image{w,h,Bytes(size_t(w)*h*4)};
    for(size_t i=0;i<image.rgba.size();i+=4)std::copy(color.begin(),color.end(),image.rgba.begin()+long(i));
    return image;
}
// Character creation: each class's folder and file prefix under data\global\ui\frontend, where its hero stands by the fire
// (the anchor of its frames), and its description string.
struct Hero {const char* folder;const char* token;float x,y;uint32_t description;};
constexpr Hero heroes[7]={{"amazon","am",100,390,5128},{"sorceress","so",626,352,5131},{"necromancer","ne",300,335,5129},{"paladin","pa",521,338,5132},
    {"barbarian","ba",400,330,5130},{"druid","dz",720,370,22518},{"assassin","as",231,365,22519}};
constexpr float nameBoxX=318,nameBoxY=493,hardcoreX=318,hardcoreY=548,expansionY=526;
constexpr double heroFps=25;
void syncWebStorage()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var fs = typeof FS !== 'undefined' ? FS : (typeof Module !== 'undefined' ? Module.FS : null);
        if (fs && fs.syncfs) fs.syncfs(false, function(err) { if (err) console.error(err); });
    });
#endif
}
}
Bytes newCharacterSave(const std::string& name,int charClass,bool hardcore,bool expansion,uint32_t createdAt) {
    Bytes b(335,0);
    auto put32=[&](size_t o,uint32_t v){for(int i=0;i<4;++i)b[o+size_t(i)]=uint8_t(v>>(8*i));};
    put32(0,0xAA55AA55u);put32(4,96);put32(8,uint32_t(b.size())); // magic, version 96 (1.10), file size
    std::copy_n(name.begin(),std::min<size_t>(name.size(),15),b.begin()+0x14);
    b[0x24]=uint8_t(0x01|(hardcore?0x04:0)|(expansion?0x20:0));
    b[0x28]=uint8_t(charClass);b[0x29]=0x10;b[0x2A]=0x1E;b[0x2B]=1; // class, two constants every 1.10 save has, level 1
    put32(0x2C,createdAt);put32(0x30,createdAt);
    // Character-select appearance: light armour on head, torso, legs, arms and specials, nothing in the hands; no colours.
    static const uint8_t look[16]={1,1,1,1,1,0xff,0xff,0xff,1,1,0xff,0xff,0xff,0xff,0xff,0xff};
    std::copy_n(look,16,b.begin()+0x88);std::fill_n(b.begin()+0x98,16,uint8_t(0xff));
    uint32_t sum=0;for(size_t i=0;i<b.size();++i)sum=((sum<<1)|(sum>>31))+(i>=12&&i<16?0u:b[i]);
    put32(12,sum);
    return b;
}
bool validCharacterName(const std::string& name) {
    if(name.size()<2||name.size()>15||!std::isalpha(static_cast<unsigned char>(name.front()))||!std::isalpha(static_cast<unsigned char>(name.back())))return false;
    int separators=0;
    for(unsigned char c:name){if(c=='-'||c=='_')++separators;else if(!std::isalpha(c))return false;}
    return separators<=1;
}
std::vector<SavedCharacter> findCharacters(const std::vector<std::filesystem::path>& folders) {
    std::map<std::wstring,SavedCharacter> byFile;
    for(const auto& folder:folders) {
        std::error_code error;
        for(const auto& entry:std::filesystem::directory_iterator(folder,error)) {
            if(!entry.is_regular_file()||entry.path().extension()!=L".d2s")continue;
            std::ifstream in(entry.path(),std::ios::binary);
            Bytes b((std::istreambuf_iterator<char>(in)),{});
            // d2s header: 0xAA55AA55, version, ..., name at 0x14, status at 0x24 (4 hardcore, 8 died, 0x20 expansion), class 0x28, level 0x2B.
            if(b.size()<0x30||read32(b,0)!=0xAA55AA55u)continue;
            SavedCharacter c;c.path=entry.path();
            for(size_t i=0x14;i<0x24&&b[i];++i)c.name.push_back(char(b[i]));
            c.hardcore=b[0x24]&0x04;c.dead=b[0x24]&0x08;c.expansion=b[0x24]&0x20;c.charClass=b[0x28];c.level=b[0x2B];c.progression=b[0x25];
            std::wstring key=entry.path().filename().wstring();std::transform(key.begin(),key.end(),key.begin(),::towlower);
            byFile[key]=c;
        }
    }
    std::vector<SavedCharacter> out;
    for(auto& [key,c]:byFile)out.push_back(std::move(c));
    return out;
}
uint32_t characterMapSeed(const std::filesystem::path& save,const std::filesystem::path& profile,bool repeatable,uint32_t fallback) {
    const auto copy=profile/save.filename();
    std::ifstream in(!repeatable&&std::filesystem::exists(copy)?copy:save,std::ios::binary); // test runs never use the profile
    Bytes b((std::istreambuf_iterator<char>(in)),{});
    if(b.size()>=0xAF&&read32(b,0)==0xAA55AA55u)if(const uint32_t seed=read32(b,0xAB))return seed;
    if(repeatable)return fallback;
    std::random_device random;uint32_t seed=0;
    while(!seed)seed=random();
    return seed;
}
void saveAppearance(const std::filesystem::path& file,const CharacterAppearance& appearance) {
    std::ofstream out(file);
    out<<appearance.token<<"\n"<<appearance.weaponClass<<"\n";
    for(const auto& component:appearance.components)out<<(component.empty()?"-":component)<<"\n";
    out.close();
    syncWebStorage();
}
std::optional<CharacterAppearance> loadAppearance(const std::filesystem::path& file) {
    std::ifstream in(file);if(!in)return std::nullopt;
    CharacterAppearance a;
    if(!(in>>a.token>>a.weaponClass))return std::nullopt;
    for(auto& component:a.components){if(!(in>>component))return std::nullopt;if(component=="-")component.clear();}
    return a;
}
FrontEnd::FrontEnd(const Archives& archives,std::vector<SavedCharacter> characters,std::vector<std::filesystem::path> appearanceFolders,const std::filesystem::path& preferred,
    std::filesystem::path newCharacterFolder)
    :archives_(archives),characters_(std::move(characters)),appearanceFolders_(std::move(appearanceFolders)),newCharacterFolder_(std::move(newCharacterFolder)) {
    for(const char* name:{"sky","units","loading","fechar"})palettes_[name]=decodePalette(archives.read(std::string("data\\global\\palette\\")+name+"\\pal.dat"));
    buttonFont_=std::make_unique<Font>(archives,palettes_["units"],"fontexocet10");
    textFont_=std::make_unique<Font>(archives,palettes_["units"],"font16");
    titleFont_=std::make_unique<Font>(archives,palettes_["units"],"font42");
    headingFont_=std::make_unique<Font>(archives,palettes_["units"],"font30");
    buttonText_=std::make_unique<TextCache>(*buttonFont_);text_=std::make_unique<TextCache>(*textFont_);titleText_=std::make_unique<TextCache>(*titleFont_);headingText_=std::make_unique<TextCache>(*headingFont_);
    // The TCP/IP screen's fonts: its descriptions, the address box and the message popup.
    formal11Font_=std::make_unique<Font>(archives,palettes_["units"],"fontformal11");formal11Text_=std::make_unique<TextCache>(*formal11Font_);
    formal12Font_=std::make_unique<Font>(archives,palettes_["units"],"fontformal12");formal12Text_=std::make_unique<TextCache>(*formal12Font_);
    font24Font_=std::make_unique<Font>(archives,palettes_["units"],"font24");font24Text_=std::make_unique<TextCache>(*font24Font_);
    // The menu's words from the game's own string tables.
    const StringTable strings(archives);
    for(uint32_t id:{5101u,5102u,5105u,5106u,5107u,5108u,5109u,5110u,5111u,5273u,22744u,22731u,5017u,21882u,5103u,5163u,4007u,4008u,4009u,4010u,4011u,4012u,4013u,5125u,5126u,5127u,5128u,5129u,5130u,5131u,5132u,22518u,22519u,5165u,5154u,5155u,5156u,10019u,
        5115u,5116u,5117u,5118u,5119u,5120u,5121u,5122u,5123u,5124u,5211u})strings_[id]=strings.byId(id);
    // The credits, as the original scrolls them: data\local\ui\eng\ExpansionCredits.txt (UTF-16) then credits.txt; a line
    // starting with * is a heading.
    for(const char* file:{"data\\local\\ui\\eng\\ExpansionCredits.txt","data\\local\\ui\\eng\\credits.txt"}) {
        Bytes bytes;try{bytes=archives.read(file);}catch(const std::exception&){continue;}
        std::string text;
        if(bytes.size()>=2&&bytes[0]==0xFF&&bytes[1]==0xFE)for(size_t i=2;i+1<bytes.size();i+=2){const unsigned c=bytes[i]|(bytes[i+1]<<8);text+=char(c<256?c:'?');}
        else text.assign(bytes.begin(),bytes.end());
        size_t at=0;
        while(at<text.size()) {
            size_t end=text.find('\n',at);if(end==std::string::npos)end=text.size();
            std::string line=text.substr(at,end-at);at=end+1;
            while(!line.empty()&&(line.back()=='\r'||line.back()==' '))line.pop_back();
            const bool heading=!line.empty()&&line[0]=='*';
            credits_.push_back({heading?line.substr(1):line,heading});
        }
        credits_.push_back({"",false});credits_.push_back({"",false});
    }
    // Project credits follow the credits supplied by the player's own game archives.
    credits_.push_back({"D2Next",true});
    credits_.push_back({"Portable game client and 64-bit engine integration",false});
    credits_.push_back({"Built on D2MOO and the Phrozen Keep community's research",false});
    credits_.push_back({"SDL, StormLib, doctest, squall, and Diablerie contributors",false});
    credits_.push_back({"Diablo II and its artwork are by Blizzard Entertainment",false});
    auto lower=[](std::wstring t){std::transform(t.begin(),t.end(),t.begin(),::towlower);return t;};
    for(size_t i=0;i<characters_.size();++i)if(lower(characters_[i].path.filename().wstring())==lower(preferred.filename().wstring()))selected_=int(i);
}
const Image* FrontEnd::frame(const std::string& file,uint32_t index,const std::string& palette) {
    const auto key=std::make_tuple(file,index,palette);
    if(auto found=frames_.find(key);found!=frames_.end())return found->second.get();
    std::unique_ptr<Image> image;
    try {
        auto bytes=files_.find(file);
        if(bytes==files_.end())bytes=files_.emplace(file,archives_.read("data\\global\\ui\\"+file+".dc6")).first;
        image=std::make_unique<Image>(decodeDc6(bytes->second,palettes_[palette],index));
    } catch(const std::exception&) {}
    return frames_.emplace(key,std::move(image)).first->second.get();
}
std::array<int32_t,2> FrontEnd::offset(const std::string& file,uint32_t index) {
    auto bytes=files_.find(file);
    try {
        if(bytes==files_.end())bytes=files_.emplace(file,archives_.read("data\\global\\ui\\"+file+".dc6")).first;
        return dc6FrameOffset(bytes->second,index);
    } catch(const std::exception&) {return {0,0};}
}
std::array<float,2> FrontEnd::buttonSize(int type) {
    return type==Wide?std::array<float,2>{272,35}:type==Medium?std::array<float,2>{128,35}:type==Small||type==Popup?std::array<float,2>{96,32}:std::array<float,2>{168,60};
}
void FrontEnd::playSound(const std::string& path) {
    if(soundPlayer_)soundPlayer_(path);
}
void FrontEnd::clickSound() {playSound("cursor\\button.wav");}
void FrontEnd::selectSound() {playSound("cursor\\select.wav");}
void FrontEnd::enableLan(std::function<std::string()> localAddress,std::function<bool(const std::string&)> validAddress,std::string lastAddress) {
    findLocalAddress_=std::move(localAddress);validAddress_=std::move(validAddress);ipText_=std::move(lastAddress);
}
void FrontEnd::showLanMessage(const std::string& message) {
    screen_=Screen::TcpIp;lan_=Lan::None;ipPopup_=false;ipFieldFocused_=false;lanMessage_=message.empty()?strings_[5211]:message;
    if(findLocalAddress_)localAddress_=findLocalAddress_();
}
std::vector<std::string> FrontEnd::wrap(const Font& font,const std::string& text,float width) const {
    std::vector<std::string> lines{""};
    for(size_t at=0;at<=text.size();) {
        auto end=text.find(' ',at);if(end==std::string::npos)end=text.size();
        const auto word=text.substr(at,end-at);
        if(!lines.back().empty()&&float(font.width(lines.back()+" "+word))>width)lines.push_back(word);
        else lines.back()+=(lines.back().empty()?"":" ")+word;
        at=end+1;
    }
    return lines;
}
std::vector<FrontEnd::Button> FrontEnd::buttons() const {
    auto s=[&](uint32_t id){auto found=strings_.find(id);return found==strings_.end()?std::string():found->second;};
    if(screen_==Screen::MainMenu)return {
        {s(5106),264,290,Wide,SinglePlayer,true},
        {"MULTIPLAYER",264,332,Wide,OtherMultiplayer,bool(findLocalAddress_)},
        {s(5110),264,495,Medium,Credits,true},
        {s(5111),401,495,Medium,Cinematics,true},
        {s(5109),264,535,Wide,ExitGame,true}};
    if(screen_==Screen::CharacterSelect) {
        // "CREATE NEW CHARACTER" and "DELETE CHARACTER" on two lines, as on the tall buttons.
        auto twoLines=[](std::string t){const auto space=t.rfind(' ');if(space!=std::string::npos)t[space]='\n';return t;};
        std::vector<Button> list{
            {twoLines(s(5273)),33,468,Tall,CreateCharacter,!newCharacterFolder_.empty()},
            {twoLines(s(22744)),433,468,Tall,DeleteCharacter,deletable(selected_)},
            {s(5101),33,537,Medium,ExitSelect,true},
            {s(5102),625,537,Medium,Ok,!characters_.empty()}};
        if(chooseDifficulty_) {
            // SELECT DIFFICULTY is up: its rows answer (difficultyRow), not these buttons.
            for(auto& b:list)b.enabled=false;
        }
        if(confirmDelete_) {
            // While the delete popup is up only its OK and CANCEL answer.
            for(auto& b:list)b.enabled=false;
            list.push_back({s(5102),282,308,Small,ConfirmDelete,true});list.push_back({s(5103),422,308,Small,CancelDelete,true});
        }
        return list;
    }
    if(screen_==Screen::CreateCharacter)return {{s(5101),33,537,Medium,ExitCreate,true},{s(5102),630,537,Medium,OkCreate,canCreate()}};
    if(screen_==Screen::OtherMultiplayer)return {{s(5115),264,276,Wide,OpenBattleNet,false},{s(5116),264,316,Wide,TcpIpGame,true},{s(5103),264,534,Wide,CancelMultiplayer,true}};
    if(screen_==Screen::Credits)return {{s(5103),264,535,Wide,BackToMainMenu,true}};
    if(screen_==Screen::Cinematics)return {
        {"INTRODUCTION",264,205,Wide,PlayIntro,true},
        {"ACT I",264,247,Wide,PlayAct1,true},
        {"ACT II",264,289,Wide,PlayAct2,true},
        {"ACT III",264,331,Wide,PlayAct3,true},
        {"ACT IV",264,373,Wide,PlayAct4,true},
        {"ACT V",264,415,Wide,PlayAct5,true},
        {s(5103),264,535,Wide,BackToMainMenu,true}};
    if(screen_==Screen::TcpIp) {
        // HOST GAME and JOIN GAME only with an address to play on, as in the original.
        std::vector<Button> list{{s(5118),hostX,hostY,Wide,HostGame,!localAddress_.empty()},{s(5119),hostX,joinY,Wide,JoinGame,!localAddress_.empty()},
            {s(5103),39,537,Medium,CancelTcpIp,true}};
        if(ipPopup_||!lanMessage_.empty())for(auto& b:list)b.enabled=false; // only a popup's buttons answer while it is up
        if(ipPopup_){list.push_back({s(5103),281,306,Popup,AddressCancel,true});list.push_back({s(5102),421,306,Popup,AddressOk,!ipText_.empty()});}
        else if(!lanMessage_.empty())list.push_back({s(5102),351,306,Popup,MessageOk,true});
        return list;
    }
    return {};
}
void FrontEnd::activate(int action) {
    clickSound();
    switch(action) {
    case SinglePlayer:lan_=Lan::None;screen_=Screen::CharacterSelect;break;
    case ExitGame:quit_=true;break;
    case Credits:screen_=Screen::Credits;break;
    case Cinematics:screen_=Screen::Cinematics;cinematicMessage_.clear();break;
    case BackToMainMenu:stopCinematic();screen_=Screen::MainMenu;cinematicMessage_.clear();break;
    case PlayIntro:playCinematic(0);break;
    case PlayAct1:playCinematic(1);break;
    case PlayAct2:playCinematic(2);break;
    case PlayAct3:playCinematic(3);break;
    case PlayAct4:playCinematic(4);break;
    case PlayAct5:playCinematic(5);break;
    // Out of character selection: back where it was entered from (the TCP/IP screen for a TCP/IP game).
    case ExitSelect:screen_=lan_==Lan::None?Screen::MainMenu:Screen::TcpIp;lan_=Lan::None;break;
    case OtherMultiplayer:screen_=Screen::OtherMultiplayer;break;
    case CancelMultiplayer:screen_=Screen::MainMenu;break;
    case TcpIpGame:
        // The TCP/IP screen shows this computer's address, found as it opens.
        screen_=Screen::TcpIp;localAddress_=findLocalAddress_?findLocalAddress_():std::string();break;
    case CancelTcpIp:screen_=Screen::OtherMultiplayer;break;
    case HostGame:lan_=Lan::Host;screen_=Screen::CharacterSelect;break;
    case JoinGame:ipPopup_=true;ipFieldFocused_=false;break;
    case AddressCancel:ipPopup_=false;ipFieldFocused_=false;break;
    // An address that names no computer leaves the box open, as the original does.
    case AddressOk:if(!ipText_.empty()&&validAddress_&&validAddress_(ipText_)){ipPopup_=false;ipFieldFocused_=false;lan_=Lan::Join;screen_=Screen::CharacterSelect;}break;
    case MessageOk:lanMessage_.clear();break;
    case Ok:choose();break;
    case PlayNormal:case PlayNightmare:case PlayHell:difficulty_=action-PlayNormal;chooseDifficulty_=false;screen_=Screen::Loading;break;
    case CancelDifficulty:chooseDifficulty_=false;break;
    case CreateCharacter:
        screen_=Screen::CreateCharacter;newClass_=hoverClass_=-1;newName_.clear();newHardcore_=false;newExpansion_=true;createError_.clear();
        walkedForward_.fill(-1);walkedBack_.fill(-1);
        break;
    case ExitCreate:screen_=Screen::CharacterSelect;break;
    case DeleteCharacter:confirmDelete_=true;break;
    case CancelDelete:confirmDelete_=false;break;
    case ConfirmDelete:deleteSelected();break;
    case OkCreate:create();break;
    default:break;
    }
}
std::optional<std::filesystem::path> FrontEnd::extractCinematic(const std::vector<std::string>& candidates,std::string& tried) const {
    tried.clear();
    for(const auto& name:candidates) {
        if(!tried.empty())tried+=", ";
        tried+=name;
        auto folder=std::filesystem::temp_directory_path()/L"D2Next-video";
        std::error_code error;std::filesystem::create_directories(folder,error);
        std::string file=name;
        std::replace(file.begin(),file.end(),'\\','_');std::replace(file.begin(),file.end(),'/','_');
        const auto path=folder/std::filesystem::path(file).filename();
        if(std::filesystem::is_regular_file(path,error)&&std::filesystem::file_size(path,error)>0)return path;
        try {
            const auto bytes=archives_.read(name,512ull*1024*1024);
            std::ofstream out(path,std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            if(out)return path;
        } catch(const std::exception&) {}
    }
    return std::nullopt;
}
void FrontEnd::playCinematic(int cinematic) {
    static const std::vector<std::vector<std::string>> candidates={
        {"data\\local\\video\\Eng\\d2intro640x146.bik","data\\local\\video\\Eng\\d2intro640x292.bik"},
        {"data\\local\\video\\Eng\\d2intro640x146.bik","data\\local\\video\\Eng\\d2intro640x292.bik"},
        {"data\\local\\video\\Eng\\Act02start640x292.bik","data\\local\\video\\Eng\\Act02start640x146.bik"},
        {"data\\local\\video\\Eng\\Act03start640x292.bik","data\\local\\video\\Eng\\Act03start640x146.bik"},
        {"data\\local\\video\\Eng\\Act04start640x292.bik","data\\local\\video\\Eng\\Act04end640x292.bik","data\\local\\video\\Eng\\Act04start640x146.bik","data\\local\\video\\Eng\\Act04end640x146.bik"},
        {"Data\\Local\\Video\\ENG\\D2x_Intro_640x292.bik","Data\\Local\\Video\\ENG\\D2x_Out_640x292.bik","Data\\Local\\Video\\ENG\\D2x_Intro_640x146.bik","Data\\Local\\Video\\ENG\\D2x_Out_640x146.bik"}
    };
    cinematicMessage_.clear();cinematicVideo_.reset();
    if(cinematic<0||cinematic>=int(candidates.size()))return;
    std::string tried;
    const auto path=extractCinematic(candidates[size_t(cinematic)],tried);
    if(!path){cinematicMessage_="Movie not found in video MPQs.";return;}
    cinematicPath_=*path;
    if(startCinematicAt(0)){if(musicPlayer_)musicPlayer_("");screen_=Screen::CinematicPlayer;}
}
bool FrontEnd::startCinematicAt(double seconds) {
    if(cinematicPath_.empty())return false;
    seconds=std::max(0.0,seconds);
    if(cinematicDuration_>0)seconds=std::min(seconds,std::max(0.0,cinematicDuration_-.1));
    cinematicVideo_.reset();cinematicAudio_.reset();
    auto video=std::make_unique<VideoDecoder>(cinematicPath_,seconds);
    if(!video->readFrame(cinematicFrame_)) {
        cinematicMessage_=video->error().empty()?"FFmpeg could not decode this movie.":video->error();
        return false;
    }
    auto& pcm=cinematicAudioCache_[cinematicPath_.string()];
    if(pcm.samples.empty())pcm=VideoDecoder::decodeAudio(cinematicPath_);
    if(!pcm.samples.empty()) {
        cinematicDuration_=double(pcm.samples.size())/(double(pcm.rate)*pcm.channels*2.0);
        if(movieAudioPlayer_)cinematicAudio_=movieAudioPlayer_(pcm.samples,pcm.rate,pcm.channels,seconds);
    }
    if(cinematicDuration_<=0)cinematicDuration_=std::max(cinematicDuration_,seconds+1);
    cinematicVideo_=std::move(video);
    cinematicPosition_=seconds;
    cinematicFrameAt_=time_;
    return true;
}
void FrontEnd::stopCinematic() {
    cinematicVideo_.reset();cinematicAudio_.reset();cinematicSeeking_=false;
    if(musicPlayer_)musicPlayer_("data\\global\\music\\common\\options.wav");
}
bool FrontEnd::overCinematicBar(float x,float y) const {
    return screen_==Screen::CinematicPlayer&&cinematicDuration_>0&&x>=movieBarX-10&&x<=movieBarX+movieBarW+10&&y>=movieBarY-12&&y<=movieBarY+20;
}
void FrontEnd::seekCinematic(float x) {
    if(cinematicDuration_<=0)return;
    const float t=std::clamp((x-movieBarX)/movieBarW,0.f,1.f);
    startCinematicAt(double(t)*cinematicDuration_);
}
int FrontEnd::difficultyRow(float x,float y) const {
    if(x<difficultyX||x>=difficultyX+difficultyW)return -1;
    for(int row=0;row<3;++row)if(y>=difficultyRowY[row]&&y<difficultyRowY[row]+difficultyRowH)return row;
    return -1;
}
int FrontEnd::difficultiesOpen() const {return selected_>=0&&selected_<int(characters_.size())?characters_[size_t(selected_)].difficulties():1;}
void FrontEnd::choose() {
    if(selected_<0||selected_>=int(characters_.size()))return;
    // A character that has opened Nightmare first chooses the difficulty (D2Launch's DifficultyLevels popup); one that has
    // not plays Normal.
    difficulty_=0;
    if(characters_[size_t(selected_)].difficulties()>1)chooseDifficulty_=true;else screen_=Screen::Loading;
}
int FrontEnd::slotAt(float x,float y) const {
    if(screen_!=Screen::CharacterSelect||x<slotX||x>=slotX+2*slotW||y<slotY||y>=slotY+4*slotH)return -1;
    const int slot=(firstRow_+int((y-slotY)/slotH))*2+int((x-slotX)/slotW);
    return slot<int(characters_.size())?slot:-1;
}
int FrontEnd::maxFirstRow() const {
    return std::max(0,int(characters_.size()+1)/2-4);
}
void FrontEnd::setFirstRow(int row) {
    firstRow_=std::clamp(row,0,maxFirstRow());
}
void FrontEnd::ensureSelectedVisible() {
    if(selected_<0)return;
    const int row=selected_/2;
    if(row<firstRow_)setFirstRow(row);
    else if(row>=firstRow_+4)setFirstRow(row-3);
    else setFirstRow(firstRow_);
}
bool FrontEnd::overSaveScroll(float x,float y) const {
    return screen_==Screen::CharacterSelect&&maxFirstRow()>0&&x>=saveScrollX&&x<saveScrollX+saveScrollW&&y>=saveScrollY&&y<saveScrollY+saveScrollH;
}
float FrontEnd::saveScrollThumbY() const {
    const int maxRow=maxFirstRow();
    const float trackTop=saveScrollY+saveScrollButtonH,trackH=saveScrollH-saveScrollButtonH*2-saveScrollThumbH;
    return trackTop+(maxRow>0?trackH*float(firstRow_)/float(maxRow):0.f);
}
void FrontEnd::dragSaveScroll(float y) {
    const int maxRow=maxFirstRow();if(maxRow<=0)return;
    const float trackTop=saveScrollY+saveScrollButtonH,trackH=saveScrollH-saveScrollButtonH*2-saveScrollThumbH;
    const float t=trackH>0?std::clamp((y-saveScrollDragOffset_-trackTop)/trackH,0.f,1.f):0.f;
    setFirstRow(int(std::round(t*float(maxRow))));
}
void FrontEnd::mouseMove(float x,float y){
    mouseX_=x;mouseY_=y;
    if(screen_==Screen::CinematicPlayer&&cinematicSeeking_&&cinematicDuration_>0){
        cinematicPosition_=double(std::clamp((x-movieBarX)/movieBarW,0.f,1.f))*cinematicDuration_;
        return;
    }
    if(screen_==Screen::CharacterSelect&&draggingSaveScroll_){dragSaveScroll(y);return;}
    if(screen_==Screen::CreateCharacter)hoverClass_=heroAt(x,y);
}
void FrontEnd::mouseDown(float x,float y) {
    mouseX_=x;mouseY_=y;
    if(screen_==Screen::Title){selectSound();screen_=Screen::MainMenu;return;}
    if(screen_==Screen::CinematicPlayer){
        if(overCinematicBar(x,y)){cinematicSeeking_=true;seekCinematic(x);return;}
        clickSound();stopCinematic();screen_=Screen::Cinematics;return;
    }
    if(chooseDifficulty_){const int row=difficultyRow(x,y);if(row>=0&&row<difficultiesOpen()){clickSound();activate(PlayNormal+row);}return;}
    const auto list=buttons();
    for(size_t i=0;i<list.size();++i){const auto size=buttonSize(list[i].type);if(list[i].enabled&&x>=list[i].x&&x<list[i].x+size[0]&&y>=list[i].y&&y<list[i].y+size[1]){pressed_=int(i);return;}}
    if(ipPopup_){ipFieldFocused_=addressFieldAt(x,y);if(ipFieldFocused_)return;}
    if(screen_==Screen::CreateCharacter) {
        if(newClass_>=0&&x>=hardcoreX&&x<hardcoreX+15&&y>=hardcoreY&&y<hardcoreY+16){newHardcore_=!newHardcore_;return;}
        // The expansion box: the Druid and the Assassin are expansion characters only.
        if(newClass_>=0&&newClass_<5&&x>=hardcoreX&&x<hardcoreX+15&&y>=expansionY&&y<expansionY+16){newExpansion_=!newExpansion_;return;}
        if(const int hero=heroAt(x,y);hero>=0)chooseClass(hero);
        return;
    }
    if(confirmDelete_||chooseDifficulty_)return; // only the popup's buttons answer while it is up
    if(overSaveScroll(x,y)) {
        const float thumbY=saveScrollThumbY();
        const int before=firstRow_;
        if(y<saveScrollY+saveScrollButtonH)setFirstRow(firstRow_-1);
        else if(y>=saveScrollY+saveScrollH-saveScrollButtonH)setFirstRow(firstRow_+1);
        else if(y>=thumbY&&y<thumbY+saveScrollThumbH){draggingSaveScroll_=true;saveScrollDragOffset_=y-thumbY;}
        else setFirstRow(firstRow_+(y<thumbY?-4:4));
        if(firstRow_!=before)selectSound();
        return;
    }
    if(const int slot=slotAt(x,y);slot>=0) {
        // A second click on the same character soon after the first plays it, as a double click does in the game.
        if(slot==lastClickSlot_&&time_-lastClickTime_<.5){clickSound();choose();return;}
        if(selected_!=slot)selectSound();
        selected_=slot;lastClickSlot_=slot;lastClickTime_=time_;
    }
}
bool FrontEnd::nameFieldAt(float x,float y) const {
    return screen_==Screen::CreateCharacter&&newClass_>=0&&
        x>=nameBoxX&&x<nameBoxX+164&&y>=nameBoxY&&y<nameBoxY+32;
}
bool FrontEnd::addressFieldAt(float x,float y) const {
    return ipPopup_&&x>=addressBoxX&&x<addressBoxX+218&&y>=addressBoxY&&y<addressBoxY+32;
}
void FrontEnd::mouseUp(float x,float y) {
    mouseX_=x;mouseY_=y;
    if(screen_==Screen::CinematicPlayer&&cinematicSeeking_){seekCinematic(x);cinematicSeeking_=false;return;}
    if(screen_==Screen::CharacterSelect&&draggingSaveScroll_){dragSaveScroll(y);draggingSaveScroll_=false;return;}
    const auto list=buttons();
    if(pressed_>=0&&pressed_<int(list.size())) {
        const auto& b=list[size_t(pressed_)];const auto size=buttonSize(b.type);
        pressed_=-1;
        if(x>=b.x&&x<b.x+size[0]&&y>=b.y&&y<b.y+size[1])activate(b.action);
    }
    pressed_=-1;
}
void FrontEnd::wheel(int amount) {
    if(screen_!=Screen::CharacterSelect||confirmDelete_||maxFirstRow()<=0)return;
    const int before=firstRow_;
    setFirstRow(firstRow_-amount);
    if(firstRow_!=before)selectSound();
}
void FrontEnd::key(unsigned vk) {
    if(screen_==Screen::Title){selectSound();screen_=Screen::MainMenu;return;}
    if(screen_==Screen::MainMenu){if(vk==d2::key::Enter)activate(SinglePlayer);else if(vk==d2::key::Escape){clickSound();quit_=true;}return;}
    if(screen_==Screen::CinematicPlayer){if(vk==d2::key::Escape){clickSound();stopCinematic();screen_=Screen::Cinematics;}return;}
    if(screen_==Screen::Credits||screen_==Screen::Cinematics){if(vk==d2::key::Escape)activate(BackToMainMenu);return;}
    if(screen_==Screen::OtherMultiplayer){if(vk==d2::key::Escape)activate(CancelMultiplayer);return;}
    if(screen_==Screen::TcpIp) {
        // The popups' OK is Enter and CANCEL Escape (their buttons' keys in D2Launch); the screen's CANCEL is Escape.
        if(ipPopup_) {
            if(vk==d2::key::Escape)activate(AddressCancel);
            else if(vk==d2::key::Enter)activate(AddressOk);
            else if(vk==d2::key::Backspace&&!ipText_.empty())ipText_.pop_back();
        } else if(!lanMessage_.empty()){if(vk==d2::key::Enter||vk==d2::key::Escape)activate(MessageOk);}
        else if(vk==d2::key::Escape)activate(CancelTcpIp);
        return;
    }
    if(screen_==Screen::CreateCharacter) {
        if(vk==d2::key::Escape){clickSound();screen_=Screen::CharacterSelect;}
        else if(vk==d2::key::Enter&&canCreate()){clickSound();create();}
        else if(vk==d2::key::Backspace&&!newName_.empty()){newName_.pop_back();createError_.clear();}
        return;
    }
    if(screen_!=Screen::CharacterSelect||characters_.empty()){if(vk==d2::key::Escape&&screen_==Screen::CharacterSelect)activate(ExitSelect);return;}
    if(chooseDifficulty_){if(vk==d2::key::Escape){clickSound();chooseDifficulty_=false;}return;}
    if(confirmDelete_){if(vk==d2::key::Escape){clickSound();confirmDelete_=false;}else if(vk==d2::key::Enter){clickSound();deleteSelected();}return;}
    const int count=int(characters_.size());
    if(vk==d2::key::Escape)activate(ExitSelect);
    else if(vk==d2::key::Enter){clickSound();choose();}
    else if(vk==d2::key::Left){const int before=selected_;selected_=std::max(0,selected_-1);if(selected_!=before){ensureSelectedVisible();selectSound();}}
    else if(vk==d2::key::Right){const int before=selected_;selected_=std::min(count-1,selected_+1);if(selected_!=before){ensureSelectedVisible();selectSound();}}
    else if(vk==d2::key::Up){const int before=selected_;selected_=selected_>=2?selected_-2:selected_;if(selected_!=before){ensureSelectedVisible();selectSound();}}
    else if(vk==d2::key::Down){const int before=selected_;selected_=selected_+2<count?selected_+2:selected_;if(selected_!=before){ensureSelectedVisible();selectSound();}}
}
void FrontEnd::advance(double seconds){
    time_+=seconds;
    if(screen_==Screen::CinematicPlayer&&cinematicVideo_) {
        constexpr double frameSeconds=1.0/15.0;
        while(time_-cinematicFrameAt_>=frameSeconds) {
            cinematicFrameAt_+=frameSeconds;
            if(!cinematicVideo_->readFrame(cinematicFrame_)) {
                stopCinematic();
                cinematicMessage_="Movie ended.";
                screen_=Screen::Cinematics;
                break;
            }
            cinematicPosition_+=frameSeconds;
        }
    }
}
void FrontEnd::character(unsigned code) {
    // An address: digits and dots, or a computer's name (letters, digits, '-'), and ":port".
    if(ipPopup_) {
        const bool allowed=(code>='0'&&code<='9')||(code>='a'&&code<='z')||(code>='A'&&code<='Z')||code=='.'||code=='-'||code==':';
        if(allowed&&ipText_.size()<63)ipText_.push_back(char(code));
        return;
    }
    // Names take letters, '-' and '_' only; the rules for where they may stand are checked by OK.
    if(screen_!=Screen::CreateCharacter||newClass_<0||newName_.size()>=15)return;
    if((code>='a'&&code<='z')||(code>='A'&&code<='Z')||code=='-'||code=='_'){newName_.push_back(char(code));createError_.clear();}
}
uint32_t FrontEnd::frameCount(const std::string& file) {
    auto bytes=files_.find(file);
    try {
        if(bytes==files_.end())bytes=files_.emplace(file,archives_.read("data\\global\\ui\\"+file+".dc6")).first;
        return dc6FrameCount(bytes->second);
    } catch(const std::exception&) {return 0;}
}
std::string FrontEnd::heroFile(int charClass,const char* animation) const {
    return std::string("frontend\\")+heroes[charClass].folder+"\\"+heroes[charClass].token+animation;
}
int FrontEnd::heroAt(float x,float y) {
    // The hero whose standing picture (its first idle frame, or the facing one when chosen) holds the point; the front one wins.
    int best=-1;float bestY=-1;
    for(int c=0;c<7;++c) {
        const auto file=heroFile(c,c==newClass_?"nu3":"nu1");
        const auto* image=frame(file,0,"fechar");if(!image)continue;
        const auto o=offset(file,0);
        const float left=heroes[c].x+float(o[0]),top=heroes[c].y+float(o[1])-float(image->height);
        if(x>=left&&x<left+float(image->width)&&y>=top&&y<top+float(image->height)&&heroes[c].y>bestY){best=c;bestY=heroes[c].y;}
    }
    return best;
}
void FrontEnd::chooseClass(int charClass) {
    if(charClass==newClass_)return;
    // The hero chosen before walks back to the fire; the new one walks forward to face the player.
    static const char* selectSounds[]={"cursor\\intro\\amazon select.wav","cursor\\intro\\sorceress select.wav","cursor\\intro\\necromancer select.wav","cursor\\intro\\paladin select.wav","cursor\\intro\\barbarian select.wav","cursor\\intro\\druid select.wav","cursor\\intro\\assassin select.wav"};
    static const char* deselectSounds[]={"cursor\\intro\\amazon deselect.wav","cursor\\intro\\sorceress deselect.wav","cursor\\intro\\necromancer deselect.wav","cursor\\intro\\paladin deselect.wav","cursor\\intro\\barbarian deselect.wav","cursor\\intro\\druid deselect.wav","cursor\\intro\\assassin deselect.wav"};
    if(newClass_>=0&&newClass_<7){walkedBack_[size_t(newClass_)]=time_;playSound(deselectSounds[newClass_]);}
    newClass_=charClass;walkedForward_[size_t(charClass)]=time_;
    if(charClass>=0&&charClass<7)playSound(selectSounds[charClass]);
}
bool FrontEnd::deletable(int character) const {
    // Only characters in this game's own save folder: never the files of an installed Diablo II.
    if(character<0||character>=int(characters_.size())||newCharacterFolder_.empty())return false;
    std::error_code error;
    return std::filesystem::equivalent(characters_[size_t(character)].path.parent_path(),newCharacterFolder_,error);
}
void FrontEnd::deleteSelected() {
    confirmDelete_=false;
    if(!deletable(selected_))return;
    // Moved into "deleted" beside the saves rather than erased, so a character deleted by mistake can be put back.
    const auto path=characters_[size_t(selected_)].path;
    const auto bin=newCharacterFolder_/L"deleted";
    std::error_code error;std::filesystem::create_directories(bin,error);
    auto look=path;look.replace_extension(L".look");
    for(const auto& file:{path,look}) {
        if(!std::filesystem::exists(file))continue;
        auto target=bin/file.filename();
        for(int n=2;std::filesystem::exists(target);++n)target=bin/(file.stem().wstring()+L" ("+std::to_wstring(n)+L")"+file.extension().wstring());
        std::filesystem::rename(file,target,error);
    }
    syncWebStorage();
    characters_.erase(characters_.begin()+selected_);
    // Previews are cached by position in the list: the ones after the deleted character move up one.
    std::map<size_t,std::unique_ptr<Animation>> moved;
    for(auto& [index,animation]:previews_) {
        if(index==size_t(selected_))retiredPreviews_.push_back(std::move(animation));
        else moved.emplace(index>size_t(selected_)?index-1:index,std::move(animation));
    }
    previews_=std::move(moved);
    selected_=std::clamp(selected_,0,std::max(0,int(characters_.size())-1));
    ensureSelectedVisible();
}
bool FrontEnd::canCreate() const {return newClass_>=0&&validCharacterName(newName_);}
void FrontEnd::create() {
    if(!canCreate()||newCharacterFolder_.empty())return;
    auto lower=[](std::string t){std::transform(t.begin(),t.end(),t.begin(),::tolower);return t;};
    for(const auto& c:characters_)if(lower(c.name)==lower(newName_)){createError_=strings_[5165];return;}
    const auto path=newCharacterFolder_/(newName_+".d2s");
    std::error_code error;std::filesystem::create_directories(newCharacterFolder_,error);
    if(std::filesystem::exists(path)){createError_=strings_[5165];return;}
    // Every character this game makes is an expansion character: the game it plays in is Lord of Destruction.
    const bool expansion=newExpansion_||newClass_>=5;
    const auto bytes=newCharacterSave(newName_,newClass_,newHardcore_,expansion,uint32_t(std::time(nullptr)));
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!out){createError_="Cannot write "+path.string();return;}
    out.close();
    syncWebStorage();
    SavedCharacter c;c.path=path;c.name=newName_;c.charClass=newClass_;c.level=1;c.expansion=expansion;c.hardcore=newHardcore_;
    characters_.push_back(c);selected_=int(characters_.size())-1;
    screen_=Screen::Loading;
}
const Animation* FrontEnd::preview(size_t character) {
    if(auto found=previews_.find(character);found!=previews_.end())return found->second.get();
    static const char* tokens[]={"am","so","ne","pa","ba","dz","ai"};
    const auto& c=characters_[character];
    CharacterAppearance appearance;
    appearance.token=c.charClass>=0&&c.charClass<7?tokens[c.charClass]:"pa";
    for(const auto& folder:appearanceFolders_)if(auto look=loadAppearance(folder/(c.path.stem().wstring()+L".look")))appearance=*look;
    std::unique_ptr<Animation> animation;
    try {animation=std::make_unique<Animation>(loadCharacter(archives_,palettes_["units"],"tn",appearance));}
    catch(const std::exception&) {
        // Gear art missing: the plain class in town clothes.
        try {CharacterAppearance plain;plain.token=appearance.token;animation=std::make_unique<Animation>(loadCharacter(archives_,palettes_["units"],"tn",plain));}catch(const std::exception&){}
    }
    return previews_.emplace(character,std::move(animation)).first->second.get();
}
std::vector<ScreenSprite> FrontEnd::draw() {
    std::vector<ScreenSprite> out;
    auto add=[&](const Image* image,float x,float y,bool additive=false){if(image)out.push_back({image,x,y,additive});};
    auto background=[&](const std::string& file){for(uint32_t f=0;f<12;++f)add(frame(file,f,"sky"),float((f%4)*256),float((f/4)*256));};
    auto centered=[&](TextCache& cache,const std::string& text,std::array<uint8_t,3> color,float cx,float y){const auto& img=cache.get(text,color,true);add(&img,std::floor(cx-img.width*.5f),y);};
    auto logo=[&] {
        // The burning Diablo II logo, anchored at (400,120): the dark letters, then the fire added over them.
        const uint32_t f=uint32_t(time_*25)%30;
        for(const auto& [file,additive]:std::initializer_list<std::pair<const char*,bool>>{{"frontend\\d2logoblackleft",false},{"frontend\\d2logoblackright",false},{"frontend\\d2logofireleft",true},{"frontend\\d2logofireright",true}}) {
            const auto* image=frame(file,f,"units");if(!image)continue;
            const auto o=offset(file,f);
            add(image,400.f+o[0],120.f+o[1]-float(image->height),additive);
        }
    };
    // All the buttons, or (TCP/IP screen, under and over its popups) only the screen's or only the popup's.
    enum Which {All,ScreenOnly,PopupOnly};
    auto drawButtons=[&](Which which=All) {
        const auto list=buttons();
        for(size_t i=0;i<list.size();++i) {
            const auto& b=list[i];const auto size=buttonSize(b.type);
            if((which==ScreenOnly&&b.type==Popup)||(which==PopupOnly&&b.type!=Popup))continue;
            const bool down=pressed_==int(i)&&mouseX_>=b.x&&mouseX_<b.x+size[0]&&mouseY_>=b.y&&mouseY_<b.y+size[1];
            if(b.type==Wide){add(frame("frontend\\widebuttonblank",down?2:0,"units"),b.x,b.y);add(frame("frontend\\widebuttonblank",down?3:1,"units"),b.x+256,b.y);}
            else if(b.type==Small)add(frame("frontend\\okcancelbuttonblank",down?1:0,"fechar"),b.x,b.y); // popup buttons use the front-end character palette
            else if(b.type==Popup)add(frame("frontend\\cancelbuttonblank",down?1:0,"sky"),b.x,b.y);
            else add(frame(b.type==Medium?"frontend\\mediumbuttonblank":"charselect\\tallbuttonblank",down?1:0,"units"),b.x,b.y);
            const auto& label=buttonText_->get(b.label,b.enabled?buttonColor:disabledColor,true);
            add(&label,std::floor(b.x+(size[0]-label.width)*.5f)-(down?2.f:0.f),std::floor(b.y+(size[1]-label.height)*.5f)+(down?2.f:0.f));
        }
    };
    switch(screen_) {
    case Screen::Title:
        background("frontend\\trademarkscreenexp");logo();
        centered(*text_,strings_[21882],lightBrown,400,500);
        centered(*text_,strings_[5105],lightBrown,400,525);
        break;
    case Screen::MainMenu:
    case Screen::OtherMultiplayer: // its buttons take the main menu's place on the same background
        background("frontend\\gameselectscreenexp");logo();drawButtons();
        break;
    case Screen::Credits: {
        // The game's credits rising over charselect\creditsbckgexpand: headings in gold, names in white.
        background("charselect\\creditsbckgexpand");
        constexpr float row=20.f;
        const float span=row*float(credits_.size())+520.f;
        float y=520.f-std::fmod(float(time_*30.0),span);
        for(const auto& [line,heading]:credits_) {
            if(y>-24&&y<520&&!line.empty())centered(*text_,line,heading?gold:white,400,y);
            y+=row;
        }
        drawButtons();
        break;
    }
    case Screen::Cinematics: {
        background("frontend\\gameselectscreenexp");logo();
        centered(*titleText_,strings_[5111].empty()?"CINEMATICS":strings_[5111],white,400,158);
        centered(*text_,"Select a movie",lightBrown,400,184);
        drawButtons();
        if(!cinematicMessage_.empty())centered(*text_,cinematicMessage_,red,400,470);
        break;
    }
    case Screen::CinematicPlayer: {
        if(!cinematicFrame_.rgba.empty())out.push_back({&cinematicFrame_,80,60,false,true});
        else {background("frontend\\gameselectscreenexp");centered(*text_,"Loading movie...",white,400,300);}
        if(cinematicDuration_>0) {
            if(cinematicProgressTrack_.rgba.empty())cinematicProgressTrack_=solid(uint32_t(movieBarW),uint32_t(movieBarH),{42,42,42,220});
            const float t=std::clamp(float(cinematicPosition_/cinematicDuration_),0.f,1.f);
            const uint32_t fill=std::max<uint32_t>(1,uint32_t(std::round(movieBarW*t)));
            cinematicProgressFill_=solid(fill,uint32_t(movieBarH),{190,160,95,255});
            if(cinematicProgressKnob_.rgba.empty())cinematicProgressKnob_=solid(10,18,{230,210,160,255});
            out.push_back({&cinematicProgressTrack_,movieBarX,movieBarY,false,false});
            out.push_back({&cinematicProgressFill_,movieBarX,movieBarY,false,true});
            out.push_back({&cinematicProgressKnob_,movieBarX+movieBarW*t-5,movieBarY-5,false,false});
        }
        break;
    }
    case Screen::TcpIp: {
        background("frontend\\tcpipscreen");
        // Lines of text centred on cx from y down, one font line apart.
        auto lines=[&](TextCache& cache,const std::vector<std::string>& text,std::array<uint8_t,3> color,float cx,float y) {
            for(const auto& line:text){centered(cache,line,color,cx,y);y+=float(cache.font().lineHeight());}
        };
        // "TCP/IP Options", then "Your IP Address is:" over the address (or, with none, why the buttons are off).
        centered(*titleText_,strings_[5117],white,400,37);
        const bool haveAddress=!localAddress_.empty();
        centered(*text_,strings_[5121],haveAddress?white:std::array<uint8_t,3>{150,150,150},406,117);
        centered(*text_,haveAddress?localAddress_:strings_[5124],white,401,132);
        // The description of the button under the mouse.
        auto over=[&](float y){return !ipPopup_&&lanMessage_.empty()&&haveAddress&&mouseX_>=hostX&&mouseX_<hostX+272&&mouseY_>=y&&mouseY_<y+35;};
        if(over(hostY))lines(*formal11Text_,wrap(*formal11Font_,strings_[5122],descriptionWidth),white,hostX+descriptionWidth/2,descriptionY);
        else if(over(joinY))lines(*formal11Text_,wrap(*formal11Font_,strings_[5123],descriptionWidth-4),white,hostX+descriptionWidth/2,descriptionY);
        drawButtons(ScreenOnly);
        if(ipPopup_) {
            // "Enter Host IP Address to Join Game" over the address box, in the OK/CANCEL popup.
            add(frame("frontend\\popupokcancel2",0,"sky"),addressPopupX,addressPopupY);add(frame("frontend\\popupokcancel2",1,"sky"),addressPopupX+256,addressPopupY);
            lines(*text_,wrap(*textFont_,strings_[5120],200),white,400,201);
            add(frame("frontend\\ipaddressbox",0,"sky"),addressBoxX,addressBoxY);
            const bool cursor=std::fmod(time_,1.0)<.5;
            // The glyphs sit at the bottom of the font's tall frames: the text stands on the box's bottom edge.
            const auto& typed=formal12Text_->get(ipText_+(cursor?"_":""),white);
            add(&typed,addressTextX+3,addressBoxY+26-4-float(typed.height));
        } else if(!lanMessage_.empty()) {
            add(frame("frontend\\popupok",0,"sky"),addressPopupX,addressPopupY);add(frame("frontend\\popupok",1,"sky"),addressPopupX+256,addressPopupY);
            lines(*font24Text_,wrap(*font24Font_,lanMessage_,244),white,400,201);
        }
        drawButtons(PopupOnly);
        break;
    }
    case Screen::CharacterSelect: {
        background("charselect\\characterselectscreenexp");
        if(!characters_.empty())centered(*titleText_,characters_[size_t(selected_)].name,gold,318,26);
        setFirstRow(firstRow_);
        static const uint32_t classNames[]={4011,4010,4009,4008,4007,4012,4013};
        for(int row=0;row<4;++row)for(int column=0;column<2;++column) {
            const size_t index=size_t((firstRow_+row)*2+column);if(index>=characters_.size())continue;
            const auto& c=characters_[index];const float x=slotX+column*slotW,y=slotY+row*slotH;
            if(int(index)==selected_){add(frame("charselect\\charselectbox",0,"units"),x,y);add(frame("charselect\\charselectbox",1,"units"),x+256,y);}
            // The character standing in town, facing the viewer; its feet at the bottom of the box's picture area.
            if(const auto* a=preview(index);a&&!a->frames.empty()&&a->framesPerDirection) {
                const uint32_t per=a->framesPerDirection,direction=0;
                const auto& f=a->frames[size_t(direction*per+uint32_t(time_*12)%per)];
                add(&f.image,x+40+float(f.x),y+78+float(f.y));
            }
            char level[64];std::snprintf(level,sizeof level,strings_[5017].c_str(),c.level);
            add(&text_->get(c.name,gold),x+86,y+14);
            add(&text_->get(std::string(level)+" "+(c.charClass>=0&&c.charClass<7?strings_[classNames[c.charClass]]:""),white),x+86,y+30);
            if(c.expansion)add(&text_->get(strings_[22731],green),x+86,y+46);
        }
        if(maxFirstRow()>0) {
            const auto* track=frame("frontend\\joingamescrollbars",5,"sky");
            for(float y=saveScrollY+saveScrollButtonH;y<saveScrollY+saveScrollH-saveScrollButtonH;y+=track?float(track->height):saveScrollButtonH)add(track,saveScrollX,y);
            add(frame("frontend\\joingamescrollbars",0,"sky"),saveScrollX,saveScrollY);
            add(frame("frontend\\joingamescrollbars",1,"sky"),saveScrollX,saveScrollY+saveScrollH-saveScrollButtonH);
            add(frame("frontend\\joingamescrollbars",4,"sky"),saveScrollX,saveScrollThumbY());
        }
        if(chooseDifficulty_) {
            // charselect\difficultylevels (256+70 by 200) in the middle, its title above the three choices.
            add(frame("charselect\\difficultylevels",0,"sky"),237,200);add(frame("charselect\\difficultylevels",1,"sky"),493,200);
            centered(*text_,strings_[10019],gold,400,214);
            // Normal, Nightmare and Hell in its three bars: white, gold under the mouse, grey when the character has not opened it.
            for(int row=0;row<3;++row) {
                const bool open=row<difficultiesOpen(),hot=open&&difficultyRow(mouseX_,mouseY_)==row;
                centered(*text_,strings_[uint32_t(5156-row)],!open?buttonColor:hot?gold:white,400,difficultyRowY[row]+10);
            }
        }
        if(confirmDelete_) {
            // The OK/CANCEL popup at (270,175) with the game's warning, wrapped to its width; its buttons come from buttons().
            add(frame("frontend\\popupokcancel",0,"fechar"),270,175);add(frame("frontend\\popupokcancel",1,"fechar"),526,175);
            std::vector<std::string> lines{""};
            for(size_t at=0;at<strings_[5163].size();) {
                auto end=strings_[5163].find(' ',at);if(end==std::string::npos)end=strings_[5163].size();
                const auto word=strings_[5163].substr(at,end-at);
                if(!lines.back().empty()&&textFont_->width(lines.back()+" "+word)>230)lines.push_back(word);
                else lines.back()+=(lines.back().empty()?"":" ")+word;
                at=end+1;
            }
            for(size_t i=0;i<lines.size();++i)centered(*text_,lines[i],white,402,190.f+16.f*float(i));
        }
        drawButtons();
        break;
    }
    case Screen::CreateCharacter: {
        // The heroes stand round the campfire with their backs turned; the chosen one walks forward to face the player.
        for(uint32_t f=0;f<12;++f)add(frame("frontend\\charactercreationscreenexp",f,"fechar"),float((f%4)*256),float((f/4)*256));
        auto anim=[&](const std::string& file,double since,bool loop,float ax,float ay,bool additive)->bool {
            const uint32_t count=frameCount(file);if(!count)return false;
            uint32_t f=uint32_t(std::max(0.0,time_-since)*heroFps);
            if(!loop&&f>=count)return false;
            f%=count;
            const auto* image=frame(file,f,"fechar");if(!image)return false;
            const auto o=offset(file,f);
            add(image,ax+float(o[0]),ay+float(o[1])-float(image->height),additive);
            return true;
        };
        // Back to front: the campfire among the heroes by where they stand.
        std::vector<std::pair<float,int>> order{{335.f,-1}};
        for(int c=0;c<7;++c)order.push_back({heroes[c].y,c});
        std::sort(order.begin(),order.end());
        for(const auto& [depth,c]:order) {
            if(c<0){anim("frontend\\fire",0,true,380,335,true);continue;}
            const auto& h=heroes[c];
            if(c==newClass_) {
                // Walking forward (with its glow, where the class has one), then facing the player.
                if(anim(heroFile(c,"fw"),walkedForward_[size_t(c)],false,h.x,h.y,false))anim(heroFile(c,"fws"),walkedForward_[size_t(c)],false,h.x,h.y,true);
                else {anim(heroFile(c,"nu3"),walkedForward_[size_t(c)],true,h.x,h.y,false);anim(heroFile(c,"nu3s"),walkedForward_[size_t(c)],true,h.x,h.y,true);}
            } else if(walkedBack_[size_t(c)]>=0&&anim(heroFile(c,"bw"),walkedBack_[size_t(c)],false,h.x,h.y,false)) {
                anim(heroFile(c,"bws"),walkedBack_[size_t(c)],false,h.x,h.y,true);
            } else anim(heroFile(c,c==hoverClass_?"nu2":"nu1"),0,true,h.x,h.y,false);
        }
        centered(*headingText_,strings_[5127],white,400,17);
        if(newClass_>=0) {
            static const uint32_t classNames[]={4011,4010,4009,4008,4007,4012,4013};
            centered(*headingText_,strings_[classNames[newClass_]],white,400,65);
            // The description over at most two lines.
            std::string description=strings_[heroes[newClass_].description],second;
            if(textFont_->width(description)>560)if(const auto split=description.rfind(' ',description.size()/2+8);split!=std::string::npos){second=description.substr(split+1);description.resize(split);}
            centered(*text_,description,white,400,100);
            if(!second.empty())centered(*text_,second,white,400,116);
            // Name box, the expansion box (ticked to begin with; always for the Druid and Assassin, greyed) and the hardcore box.
            add(&text_->get(strings_[5125],gold),321,475);
            add(frame("frontend\\textbox",0,"units"),nameBoxX,nameBoxY);
            const bool cursor=std::fmod(time_,1.0)<.5;
            add(&text_->get(newName_+(cursor?"_":""),white),nameBoxX+6,nameBoxY+5);
            const bool expansionOnly=newClass_>=5;
            add(frame("frontend\\clickbox",newExpansion_||expansionOnly?1:0,"units"),hardcoreX,expansionY);
            add(&text_->get(strings_[22731],expansionOnly?std::array<uint8_t,3>{150,150,150}:white),339,expansionY);
            add(frame("frontend\\clickbox",newHardcore_?1:0,"units"),hardcoreX,hardcoreY);
            add(&text_->get(strings_[5126],white),339,hardcoreY);
            if(!createError_.empty())centered(*text_,createError_,red,400,440);
        }
        drawButtons();
        break;
    }
    case Screen::Loading: {
        const auto* image=frame("loading\\loadingscreen",uint32_t(time_*10)%10,"loading");
        if(image)add(image,400.f-image->width*.5f,300.f-image->height*.5f);
        break;
    }
    }
    return out;
}
}
