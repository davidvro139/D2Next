#pragma once
#include "Assets.h"
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace d2 {
// Sound and music from the game's WAVE files (16-bit PCM), played through winmm; every sound has its own device so Windows mixes them.
class Audio {
public:
    struct Voice;
    struct Playback {
        Playback();
        ~Playback();
        Playback(const Playback&)=delete;
        Playback& operator=(const Playback&)=delete;
    private:
        friend class Audio;
        std::unique_ptr<Voice> voice;
    };
    explicit Audio(const Archives& archives);
    ~Audio();
    // Looping music by asset path (data\global\music\...); the same track again is left playing, "" stops it. A track not
    // loaded yet is read in the background (music tracks are tens of megabytes) and starts when it is ready.
    void music(const std::string& path);
    // Reads a music track in the background so that switching to it later is immediate.
    void preload(const std::string& path);
    // A one-shot effect by path relative to data\global\sfx; gain 0-1 (sounds.txt Volume / 255).
    void effect(const std::string& path,float gain=1.f);
    // The same with sounds.txt Defer Inst: not started while the voice last started with deferKey (the sound's group) is
    // still playing.
    void effect(const std::string& path,float gain,const std::string& deferKey);
    // An effect that repeats until stopLoop(key) (a state's ongoing sound: Hurricane's wind, an area's ambience). The same
    // key again keeps the one playing.
    void startLoop(const std::string& key,const std::string& path,float gain=1.f);
    void stopLoop(const std::string& key);
    // A loop's volume while it plays (0-1, times the effects volume): the rain, which follows the storm.
    void loopGain(const std::string& key,float gain);
    // An NPC speaking, by path relative to data\local\sfx (the speech archives). One at a time: a new one or stopSpeech()
    // ends the one playing.
    void speech(const std::string& path);
    void stopSpeech();
    std::unique_ptr<Playback> playPcm(Bytes pcm,uint32_t rate,uint16_t channels,double startSeconds=0);
    bool speaking() const {return speech_!=nullptr;}
    // The length of the last speech asked for (0: none, or still being read: it is read in the background).
    double speechSeconds() const {return speechSeconds_;}
    // Frees finished effects; call regularly.
    void update();
    // Enables or mutes all game audio. Muting stops current voices; enabling resumes the selected music track.
    void setEnabled(bool enabled);
    void setMusicEnabled(bool enabled);
    void setEffectsEnabled(bool enabled);
    void setMusicVolume(float volume);
    void setEffectsVolume(float volume);
    size_t playing() const;
    bool enabled() const {return enabled_;}
    bool musicEnabled() const {return musicEnabled_;}
    bool effectsEnabled() const {return effectsEnabled_;}
    float musicVolume() const {return musicVolume_;}
    float effectsVolume() const {return effectsVolume_;}
private:
    struct Streamed; // music read from its archive a second at a time while it plays (SDL build)
    struct Sound {Bytes file;const uint8_t* data=nullptr;uint32_t size=0;uint16_t channels=0,bits=0;uint32_t rate=0;std::shared_ptr<Streamed> streamed;};
    static bool decode(Bytes file,Sound& out);
    struct Loader;
    std::unique_ptr<Loader> loader_;                        // the background reader of music tracks
    std::map<std::string,std::shared_ptr<Sound>> tracks_;   // decoded music tracks kept for switching back
    void request(const std::string& path);
    std::shared_ptr<Sound> effectSound(const std::string& path); // decoded once and cached; null when missing
    std::unique_ptr<Voice> start(const std::shared_ptr<Sound>& sound,bool loop,float gain);
    const Archives& archives_;
    std::map<std::string,std::shared_ptr<Sound>> cache_;
    // Effects not decoded yet are read on the shared background thread and start when they arrive (if still recent).
    struct Waiting {float gain;std::string deferKey;std::chrono::steady_clock::time_point asked;};
    std::map<std::string,std::vector<Waiting>> waiting_;
    std::shared_ptr<BackgroundReads> background_;
    std::shared_ptr<char> life_=std::make_shared<char>(); // a reply for an Audio since destroyed is dropped
    void play(const std::shared_ptr<Sound>& sound,float gain,const std::string& deferKey);
    std::unique_ptr<Voice> music_;
    // The track left behind, fading out over a second instead of stopping at once.
    std::unique_ptr<Voice> fading_;std::chrono::steady_clock::time_point fadeStart_;
    std::unique_ptr<Voice> speech_;
    double speechSeconds_=0;
    uint64_t speechAsked_=0; // a speech read in the background plays only if no other was asked for since
    std::map<std::string,std::pair<std::string,float>> loopsWaiting_; // loops whose sound is being read: key -> path, gain
    void readInBackground(const std::string& file,std::function<void(std::shared_ptr<Sound>)> done);
    std::string musicPath_;
    std::vector<std::unique_ptr<Voice>> voices_;
    std::map<std::string,const Voice*> deferred_; // Defer Inst: the voice last started for each key
    std::map<std::string,std::unique_ptr<Voice>> loops_;
    bool enabled_=true;
    bool musicEnabled_=true,effectsEnabled_=true;
    float musicVolume_=1.f,effectsVolume_=1.f;
};
}
