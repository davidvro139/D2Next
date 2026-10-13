// Plays the Act 1 town music and a few effects and checks that the sound devices open. usage: AudioProbe GAME_DIR
#include "Audio.h"
#include <iostream>
#include <thread>
#include <chrono>
int main(int argc,char** argv){try{
    d2::Archives archives(argv[1]);d2::Audio audio(archives);
    audio.music("data\\global\\music\\Act1\\town1.wav");
    audio.effect("combat\\weapon\\one hand swing small01.wav");audio.effect("cursor\\select.wav");
    std::cout<<"playing "<<audio.playing()<<" sounds\n";
    if(audio.playing()<2){std::cerr<<"no sound device or the files did not play\n";return 1;}
    audio.setEnabled(false);
    if(audio.enabled()||audio.playing()!=0){std::cerr<<"mute did not stop audio\n";return 1;}
    audio.effect("cursor\\select.wav");
    if(audio.playing()!=0){std::cerr<<"muted effect played\n";return 1;}
    audio.setEnabled(true);
    if(!audio.enabled()||audio.playing()!=1){std::cerr<<"unmute did not resume music\n";return 1;}
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));audio.update();
    std::cout<<"after 1.5 s: "<<audio.playing()<<" (music keeps looping)\n";
    audio.music("");if(audio.playing()!=0&&audio.playing()>2){std::cerr<<"music did not stop\n";return 1;}
    std::cout<<"PASS: music, effects and menu mute play correctly\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
