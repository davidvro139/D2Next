// Sounds and music: WAVE files decoded here, played through winmm (Win32 build) or SDL3 audio streams (SDL build).
#include "Audio.h"
#ifdef D2MODERN_SDL
#include <SDL3/SDL.h>
#else
#include <windows.h>
#include <mmsystem.h>
#endif
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
namespace d2 {
// Reads music tracks on its own thread with its own archive handles, so the game never waits for a track.
struct Audio::Loader {
    std::filesystem::path root;
    std::mutex lock;std::condition_variable wake;
    std::deque<std::string> wanted;                                   // tracks to read, in order
    std::vector<std::pair<std::string,std::shared_ptr<Sound>>> ready; // read and decoded, not yet taken by the game
    bool stop=false;
    std::thread thread;
    explicit Loader(std::filesystem::path folder):root(std::move(folder)) {
#if !(defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__))
        thread=std::thread([this]{run();});
#endif
    }
    ~Loader() {{std::lock_guard<std::mutex> hold(lock);stop=true;}wake.notify_all();if(thread.joinable())thread.join();}
    void run() {
        std::unique_ptr<Archives> archives;
        for(;;) {
            std::string path;
            {
                std::unique_lock<std::mutex> hold(lock);
                wake.wait(hold,[&]{return stop||!wanted.empty();});
                if(stop)return;
                path=wanted.front();wanted.pop_front();
            }
            auto sound=std::make_shared<Sound>();
            const auto started=std::chrono::steady_clock::now();
            try {
                if(!archives)archives=std::make_unique<Archives>(root);
                if(!Audio::decode(archives->read(path),*sound))sound.reset();
            } catch(const std::exception&) {sound.reset();}
            if(std::getenv("D2MODERN_SOUND_LOG"))std::cout<<"Music loaded in the background: "<<path<<(sound?"":" (failed)")<<", "
                <<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<" ms"<<std::endl;
            std::lock_guard<std::mutex> hold(lock);ready.emplace_back(path,sound);
        }
    }
};
#ifdef D2MODERN_SDL
// One sound playing: an SDL audio stream on the default device (SDL mixes them). A looping one is fed again as it runs low.
struct Audio::Voice {
    SDL_AudioStream* stream=nullptr;std::shared_ptr<Sound> sound;bool loop=false;
    ~Voice(){if(stream)SDL_DestroyAudioStream(stream);}
    bool done() const {return !loop&&SDL_GetAudioStreamQueued(stream)==0;}
    void keep(){if(loop&&SDL_GetAudioStreamQueued(stream)<int(sound->size/2))SDL_PutAudioStreamData(stream,sound->data,int(sound->size));}
    void gain(float amount){if(stream)SDL_SetAudioStreamGain(stream,std::clamp(amount,0.f,1.f));}
};
Audio::Audio(const Archives& archives):archives_(archives),loader_(std::make_unique<Loader>(archives.root())) {
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO))fprintf(stderr,"SDL audio: %s\n",SDL_GetError());
}
Audio::~Audio() {music_.reset();speech_.reset();voices_.clear();loops_.clear();loader_.reset();SDL_QuitSubSystem(SDL_INIT_AUDIO);}
#else
// One sound playing: its own winmm device (Windows mixes them); a looping one loops in the device.
struct Audio::Voice {
    HWAVEOUT device=nullptr;WAVEHDR header{};std::shared_ptr<Sound> sound;bool prepared=false;
    ~Voice() {
        if(device){waveOutReset(device);if(prepared)waveOutUnprepareHeader(device,&header,sizeof(header));waveOutClose(device);}
    }
    bool done() const {return (header.dwFlags&WHDR_DONE)!=0;}
    void keep() {}
    void gain(float amount){if(device){const auto v=DWORD(std::clamp(amount,0.f,1.f)*65535.f);waveOutSetVolume(device,(v<<16)|v);}}
};
Audio::Audio(const Archives& archives):archives_(archives),loader_(std::make_unique<Loader>(archives.root())) {}
Audio::~Audio() {music_.reset();speech_.reset();voices_.clear();loops_.clear();loader_.reset();}
#endif
Audio::Playback::Playback()=default;
Audio::Playback::~Playback()=default;
void Audio::request(const std::string& path) {
    if(path.empty()||tracks_.count(path))return;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    tracks_[path]=nullptr;
    auto sound=std::make_shared<Sound>();
    try {
        Archives archives(loader_->root);
        if(!Audio::decode(archives.read(path),*sound))sound.reset();
    } catch(const std::exception&) {sound.reset();}
    loader_->ready.emplace_back(path,sound);
#else
    {
        std::lock_guard<std::mutex> hold(loader_->lock);
        for(const auto& w:loader_->wanted)if(w==path)return;
        tracks_[path]=nullptr; // being read
        loader_->wanted.push_back(path);
    }
    loader_->wake.notify_all();
#endif
}
void Audio::preload(const std::string& path) {request(path);}
bool Audio::decode(Bytes file,Sound& out) {
    if(file.size()<12||std::memcmp(file.data(),"RIFF",4)||std::memcmp(file.data()+8,"WAVE",4))return false;
    bool haveFormat=false;
    for(size_t p=12;p+8<=file.size();) {
        const uint32_t size=uint32_t(file[p+4])|(uint32_t(file[p+5])<<8)|(uint32_t(file[p+6])<<16)|(uint32_t(file[p+7])<<24);
        if(!std::memcmp(&file[p],"fmt ",4)&&size>=16&&p+8+16<=file.size()) {
            if((file[p+8]|(file[p+9]<<8))!=1)return false; // PCM only
            out.channels=uint16_t(file[p+10]|(file[p+11]<<8));
            out.rate=uint32_t(file[p+12])|(uint32_t(file[p+13])<<8)|(uint32_t(file[p+14])<<16)|(uint32_t(file[p+15])<<24);
            out.bits=uint16_t(file[p+22]|(file[p+23]<<8));haveFormat=true;
        } else if(!std::memcmp(&file[p],"data",4)&&haveFormat) {
            const size_t available=file.size()-(p+8);
            out.size=uint32_t(std::min<size_t>(size,available));out.file=std::move(file);out.data=out.file.data()+p+8;
            return out.size>0&&out.channels>0&&out.channels<=2&&out.bits==16;
        }
        p+=8+size+(size&1);
    }
    return false;
}
std::unique_ptr<Audio::Voice> Audio::start(const std::shared_ptr<Sound>& sound,bool loop,float gain) {
    auto voice=std::make_unique<Voice>();voice->sound=sound;
#ifdef D2MODERN_SDL
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE,int(sound->channels),int(sound->rate)};
    voice->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(!voice->stream){fprintf(stderr,"SDL audio stream: %s\n",SDL_GetError());return nullptr;}
    voice->loop=loop;
    voice->gain(std::getenv("D2MODERN_AUDIO_SILENT")?0.f:gain); // tests: the real device, heard at zero volume
    SDL_PutAudioStreamData(voice->stream,sound->data,int(sound->size));
    if(!loop)SDL_FlushAudioStream(voice->stream); // no more is coming: the resampler lets its last samples out, and the queue empties
    SDL_ResumeAudioStreamDevice(voice->stream);
    return voice;
#else
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=sound->channels;format.nSamplesPerSec=sound->rate;format.wBitsPerSample=sound->bits;
    format.nBlockAlign=WORD(format.nChannels*format.wBitsPerSample/8);format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
    if(const auto r=waveOutOpen(&voice->device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);r!=MMSYSERR_NOERROR){fprintf(stderr,"waveOutOpen error %u\n",r);voice->device=nullptr;return nullptr;}
    voice->header.lpData=reinterpret_cast<LPSTR>(const_cast<uint8_t*>(sound->data));voice->header.dwBufferLength=sound->size;
    if(loop){voice->header.dwFlags=WHDR_BEGINLOOP|WHDR_ENDLOOP;voice->header.dwLoops=0xFFFFFFFFu;}
    if(const auto r=waveOutPrepareHeader(voice->device,&voice->header,sizeof(voice->header));r!=MMSYSERR_NOERROR){fprintf(stderr,"prepare error %u\n",r);return nullptr;}
    voice->prepared=true;
    voice->gain(gain);
    if(const auto r=waveOutWrite(voice->device,&voice->header,sizeof(voice->header));r!=MMSYSERR_NOERROR){fprintf(stderr,"write error %u\n",r);return nullptr;}
    return voice;
#endif
}
void Audio::music(const std::string& path) {
    if(path==musicPath_)return;
    if(std::getenv("D2MODERN_SOUND_LOG"))std::cout<<"Music: "<<path<<std::endl;
    musicPath_=path;music_.reset();
    if(!enabled_||!musicEnabled_)return;
    if(path.empty())return;
    // Ready: play it now; otherwise it is read in the background and update() starts it.
    if(const auto found=tracks_.find(path);found!=tracks_.end()&&found->second)music_=start(found->second,true,musicVolume_);
    else request(path);
}
std::shared_ptr<Audio::Sound> Audio::effectSound(const std::string& path) {
    auto found=cache_.find(path);
    if(found==cache_.end()) {
        std::shared_ptr<Sound> sound;
        try {sound=std::make_shared<Sound>();if(!decode(archives_.read(std::string("data\\global\\sfx\\")+path),*sound))sound.reset();} catch(const std::exception&) {sound.reset();}
        found=cache_.emplace(path,sound).first;
    }
    return found->second;
}
void Audio::effect(const std::string& path,float gain) {
    // D2MODERN_SOUND_LOG: each effect asked for (even while muted), for checking which sounds fire without listening.
    static const bool log=std::getenv("D2MODERN_SOUND_LOG")!=nullptr;
    if(log)std::cout<<"Sound: "<<path<<std::endl;
    if(!enabled_||!effectsEnabled_)return;
    const auto sound=effectSound(path);
    if(!sound||voices_.size()>=24)return;
    if(auto voice=start(sound,false,effectsVolume_*gain))voices_.push_back(std::move(voice));
}
void Audio::startLoop(const std::string& key,const std::string& path,float gain) {
    static const bool log=std::getenv("D2MODERN_SOUND_LOG")!=nullptr;
    if(loops_.count(key))return;
    if(log)std::cout<<"Loop: "<<path<<std::endl;
    const auto sound=effectSound(path);
    if(!enabled_||!effectsEnabled_||!sound){loops_[key]=nullptr;return;}
    loops_[key]=start(sound,true,effectsVolume_*gain);
}
void Audio::loopGain(const std::string& key,float gain) {
    if(const auto found=loops_.find(key);found!=loops_.end()&&found->second&&!std::getenv("D2MODERN_AUDIO_SILENT"))found->second->gain(effectsVolume_*gain);
}
void Audio::stopLoop(const std::string& key) {
    if(loops_.erase(key)&&std::getenv("D2MODERN_SOUND_LOG"))std::cout<<"Loop stopped: "<<key<<std::endl;
}
void Audio::speech(const std::string& path) {
    static const bool log=std::getenv("D2MODERN_SOUND_LOG")!=nullptr;
    if(log)std::cout<<"Speech: "<<path<<std::endl;
    speech_.reset();speechSeconds_=0;
    if(path.empty())return;
    // Read when spoken (a speech is heard once, a megabyte or two), not cached. Its length is known even when muted (the
    // speech text scrolls with it).
    auto sound=std::make_shared<Sound>();
    try {if(!decode(archives_.read(std::string("data\\local\\sfx\\")+path),*sound))return;} catch(const std::exception&) {return;}
    speechSeconds_=double(sound->size)/(double(sound->rate)*sound->channels*(sound->bits/8));
    if(enabled_&&effectsEnabled_)speech_=start(sound,false,effectsVolume_);
}
void Audio::stopSpeech() {speech_.reset();}
std::unique_ptr<Audio::Playback> Audio::playPcm(Bytes pcm,uint32_t rate,uint16_t channels,double startSeconds) {
    if(!enabled_||!musicEnabled_||pcm.empty()||!rate||!channels)return {};
    constexpr uint16_t bits=16;
    const size_t block=channels*(bits/8);
    size_t offset=size_t(std::max(0.0,startSeconds)*double(rate)*double(block));
    offset-=offset%block;
    if(offset>=pcm.size())return {};
    if(offset)pcm.erase(pcm.begin(),pcm.begin()+long(offset));
    auto sound=std::make_shared<Sound>();
    sound->file=std::move(pcm);sound->data=sound->file.data();sound->size=uint32_t(sound->file.size());
    sound->channels=channels;sound->bits=bits;sound->rate=rate;
    auto playback=std::make_unique<Playback>();
    playback->voice=start(sound,false,musicVolume_);
    return playback->voice?std::move(playback):nullptr;
}
void Audio::update() {
    for(size_t i=0;i<voices_.size();) {if(voices_[i]->done())voices_.erase(voices_.begin()+long(i));else ++i;}
    if(speech_&&speech_->done())speech_.reset();
    if(music_)music_->keep();
    // Music tracks the loader finished: kept, and the one wanted now starts playing.
    std::vector<std::pair<std::string,std::shared_ptr<Sound>>> arrived;
    {std::lock_guard<std::mutex> hold(loader_->lock);arrived.swap(loader_->ready);}
    for(auto& [path,sound]:arrived) {
        if(!sound){tracks_.erase(path);continue;}
        tracks_[path]=sound;
        if(enabled_&&musicEnabled_&&path==musicPath_&&!music_)music_=start(sound,true,musicVolume_);
    }
}
void Audio::setEnabled(bool enabled) {
    if(enabled_==enabled)return;
    if(!enabled){enabled_=false;music_.reset();speech_.reset();voices_.clear();loops_.clear();return;}
    const auto track=musicPath_;musicPath_.clear();enabled_=true;
    if(!track.empty())music(track);
}
void Audio::setMusicEnabled(bool enabled) {
    if(musicEnabled_==enabled)return;
    musicEnabled_=enabled;
    const auto track=musicPath_;
    music_.reset();
    if(enabled_&&musicEnabled_&&!track.empty()) {
        musicPath_.clear();
        music(track);
    }
}
void Audio::setEffectsEnabled(bool enabled) {
    if(effectsEnabled_==enabled)return;
    effectsEnabled_=enabled;
    if(!effectsEnabled_){speech_.reset();voices_.clear();loops_.clear();}
}
void Audio::setMusicVolume(float volume) {
    musicVolume_=std::clamp(volume,0.f,1.f);
    if(music_)music_->gain(musicVolume_);
    if(musicVolume_<=0.f)setMusicEnabled(false);
}
void Audio::setEffectsVolume(float volume) {
    effectsVolume_=std::clamp(volume,0.f,1.f);
    for(auto& voice:voices_)voice->gain(effectsVolume_);
    if(speech_)speech_->gain(effectsVolume_);
    if(effectsVolume_<=0.f)setEffectsEnabled(false);
}
size_t Audio::playing() const {return voices_.size()+(music_?1:0);}
}
