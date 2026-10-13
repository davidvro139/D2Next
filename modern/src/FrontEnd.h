#pragma once
#include "Assets.h"
#include "Font.h"
#include "Video.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
namespace d2 {
// An image placed on the 800x600 menu screen (top-left corner), added onto what is below when `additive`.
struct ScreenSprite {const Image* image=nullptr;float x=0,y=0;bool additive=false,dynamic=false;};
// progression: the save's byte 0x25, the acts completed overall (an expansion character opens Nightmare at 5 and Hell at 10;
// a classic one at 4 and 8).
struct SavedCharacter {std::filesystem::path path;std::string name;int charClass=0,level=1,progression=0;bool expansion=false,hardcore=false,dead=false;
    int difficulties() const {return 1+(progression>=(expansion?5:4)?1:0)+(progression>=(expansion?10:8)?1:0);}};
class VideoDecoder;
// Characters in the save folders (*.d2s). A later folder replaces a character with the same file name from an earlier one.
std::vector<SavedCharacter> findCharacters(const std::vector<std::filesystem::path>& folders);
// The seed a character's world is built from: its save's map seed (d2s 0xAB, which D2Game writes each save), the copy in the
// profile folder taking precedence. A character that has none yet (a new one) gets a random seed of its own, so every new
// character has its own maps; `repeatable` (test runs) keeps `fallback` instead.
uint32_t characterMapSeed(const std::filesystem::path& save,const std::filesystem::path& profile,bool repeatable,uint32_t fallback);
// A new character as the original client writes it before the character's first game: a 335 byte save with name, class, level 1
// and flags only; the game gives it its starting gear and skills when it first loads it.
Bytes newCharacterSave(const std::string& name,int charClass,bool hardcore,bool expansion,uint32_t createdAt);
// Diablo II's rules for character names: 2-15 letters, at most one '-' or '_' and not at either end.
bool validCharacterName(const std::string& name);
// How the game last drew a character, remembered as NAME.look beside its profile save so character select can show the same gear.
void saveAppearance(const std::filesystem::path& file,const CharacterAppearance& appearance);
std::optional<CharacterAppearance> loadAppearance(const std::filesystem::path& file);

// The game's front end: title screen, main menu and character selection, drawn from the original art at 800x600.
class FrontEnd {
public:
    enum class Screen {Title,MainMenu,CharacterSelect,CreateCharacter,Loading,OtherMultiplayer,TcpIp,Credits,Cinematics,CinematicPlayer};
    // How the chosen character plays: alone, hosting a TCP/IP game, or joining one at lanAddress().
    enum class Lan {None,Host,Join};
    // New characters are written to newCharacterFolder.
    FrontEnd(const Archives& archives,std::vector<SavedCharacter> characters,std::vector<std::filesystem::path> appearanceFolders,const std::filesystem::path& preferred,
        std::filesystem::path newCharacterFolder={});
    using MovieAudioPlayer=std::function<std::shared_ptr<void>(Bytes,uint32_t,uint16_t,double)>;
    using SoundPlayer=std::function<void(const std::string&)>;
    using MusicPlayer=std::function<void(const std::string&)>;
    // Input in 800x600 screen coordinates.
    void mouseMove(float x,float y);
    void mouseDown(float x,float y);
    void mouseUp(float x,float y);
    bool nameFieldAt(float x,float y) const;
    bool addressFieldAt(float x,float y) const;
    void wheel(int amount);
    void key(unsigned virtualKey);
    void character(unsigned code); // a typed character (WM_CHAR)
    void advance(double seconds);
    std::vector<ScreenSprite> draw();
    Screen screen() const {return screen_;}
    bool quit() const {return quit_;}
    void showMainMenu(){screen_=Screen::MainMenu;} // back from a game: the main menu, not the title screen
    void showCharacterSelect(){screen_=Screen::CharacterSelect;} // tests: straight to the characters
    void showCredits(){screen_=Screen::Credits;}                 // tests: the credits
    void setMovieAudioPlayer(MovieAudioPlayer player){movieAudioPlayer_=std::move(player);}
    void setSoundPlayer(SoundPlayer player){soundPlayer_=std::move(player);}
    void setMusicPlayer(MusicPlayer player){musicPlayer_=std::move(player);}
    // OTHER MULTIPLAYER (TCP/IP games): this computer's address (empty: none) and the check of a join address, both
    // asked when needed; the address joined last time fills the join box. Without this the button stays disabled.
    void enableLan(std::function<std::string()> localAddress,std::function<bool(const std::string&)> validAddress,std::string lastAddress);
    // Back on the TCP/IP screen with a message (a game that could not be joined or hosted); empty: the game's own
    // "Cannot Connect to Server".
    void showLanMessage(const std::string& message={});
    Lan lan() const {return lan_;}
    const std::string& lanAddress() const {return ipText_;}
    bool wantsText() const {return screen_==Screen::CreateCharacter||(ipPopup_&&ipFieldFocused_);} // typing: a character name, or an address after tapping the box
    // The character to play once the player has chosen one (the screen is then Loading), else null.
    const SavedCharacter* chosen() const {return screen_==Screen::Loading&&selected_<int(characters_.size())?&characters_[size_t(selected_)]:nullptr;}
    int difficulty() const {return difficulty_;} // the one chosen for it: 0 Normal, 1 Nightmare, 2 Hell
private:
    struct Button {std::string label;float x,y;int type,action;bool enabled;};
    const Image* frame(const std::string& file,uint32_t index,const std::string& palette);
    std::array<int32_t,2> offset(const std::string& file,uint32_t index);
    std::vector<Button> buttons() const;
    int difficultyRow(float x,float y) const; // the SELECT DIFFICULTY row (0-2) at a point, -1 none
    int difficultiesOpen() const;             // how many the chosen character may play (1-3)
    static std::array<float,2> buttonSize(int type);
    void activate(int action);
    void playSound(const std::string& path);
    void clickSound();
    void selectSound();
    void playCinematic(int cinematic);
    bool startCinematicAt(double seconds);
    void stopCinematic();
    void seekCinematic(float x);
    bool overCinematicBar(float x,float y) const;
    std::optional<std::filesystem::path> extractCinematic(const std::vector<std::string>& candidates,std::string& tried) const;
    int slotAt(float x,float y) const;
    int maxFirstRow() const;
    void setFirstRow(int row);
    void ensureSelectedVisible();
    bool overSaveScroll(float x,float y) const;
    float saveScrollThumbY() const;
    void dragSaveScroll(float y);
    void choose();
    const Animation* preview(size_t character);
    uint32_t frameCount(const std::string& file);
    // Character creation: a class's hero at the campfire, its frame of an animation, and the class under a point (-1 none).
    std::string heroFile(int charClass,const char* animation) const;
    int heroAt(float x,float y);
    void chooseClass(int charClass);
    bool canCreate() const;
    void create();
    bool deletable(int character) const;
    void deleteSelected();
    const Archives& archives_;
    std::map<std::string,Palette> palettes_;
    std::map<std::string,Bytes> files_;
    std::map<std::tuple<std::string,uint32_t,std::string>,std::unique_ptr<Image>> frames_;
    std::unique_ptr<Font> buttonFont_,textFont_,titleFont_,headingFont_;
    std::unique_ptr<TextCache> buttonText_,text_,titleText_,headingText_;
    std::filesystem::path newCharacterFolder_;
    // Character creation: the class chosen (-1 none) and the one under the mouse, when each hero last started walking
    // forward or back (-1 never), the name typed and the hardcore box.
    int newClass_=-1,hoverClass_=-1;
    std::array<double,7> walkedForward_{-1,-1,-1,-1,-1,-1,-1},walkedBack_{-1,-1,-1,-1,-1,-1,-1};
    std::string newName_;
    bool newHardcore_=false;
    bool newExpansion_=true; // "Expansion Character": a classic character when cleared (not for the Druid and Assassin)
    std::string createError_;
    bool confirmDelete_=false; // the "delete this character?" popup is up
    std::vector<std::pair<std::string,bool>> credits_; // heading flag; original game credits followed by D2Next credits
    bool chooseDifficulty_=false;int difficulty_=0; // the SELECT DIFFICULTY popup is up; what was chosen
    // TCP/IP games: how the chosen character will play, the address box popup and its text, a message popup, this
    // computer's address (asked on entering the TCP/IP screen) and the button under the mouse there.
    Lan lan_=Lan::None;
    bool ipPopup_=false,ipFieldFocused_=false;
    std::string ipText_,lanMessage_,localAddress_;
    std::string cinematicMessage_;
    std::filesystem::path cinematicPath_;
    std::unique_ptr<VideoDecoder> cinematicVideo_;
    std::shared_ptr<void> cinematicAudio_;
    std::map<std::string,VideoDecoder::Pcm> cinematicAudioCache_;
    Image cinematicFrame_;
    Image cinematicProgressTrack_,cinematicProgressFill_,cinematicProgressKnob_;
    MovieAudioPlayer movieAudioPlayer_;
    SoundPlayer soundPlayer_;
    MusicPlayer musicPlayer_;
    double cinematicFrameAt_=0,cinematicPosition_=0,cinematicDuration_=0;
    bool cinematicSeeking_=false;
    std::function<std::string()> findLocalAddress_;
    std::function<bool(const std::string&)> validAddress_;
    std::unique_ptr<Font> formal11Font_,formal12Font_,font24Font_;
    std::unique_ptr<TextCache> formal11Text_,formal12Text_,font24Text_;
    std::vector<std::string> wrap(const Font& font,const std::string& text,float width) const;
    std::map<uint32_t,std::string> strings_;
    std::vector<SavedCharacter> characters_;
    std::vector<std::filesystem::path> appearanceFolders_;
    std::map<size_t,std::unique_ptr<Animation>> previews_;
    std::vector<std::unique_ptr<Animation>> retiredPreviews_; // of deleted characters: kept, since the renderer keys textures by image address
    Screen screen_=Screen::Title;
    int selected_=0,firstRow_=0,pressed_=-1,lastClickSlot_=-1;
    double time_=0,lastClickTime_=-10;
    float mouseX_=0,mouseY_=0;
    bool quit_=false,draggingSaveScroll_=false;
    float saveScrollDragOffset_=0;
};
}
