#include "HirelingPackets.h"
#include <array>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try {
    d2::HirelingPackets queue;std::vector<d2::HirelingOwnership> seen;
    auto apply=[&](auto ownership){seen.push_back(ownership);};
    std::array<uint8_t,20> packet{};packet[0]=0x81;packet[1]=1;packet[2]=0x23;packet[3]=1;
    packet[4]=0x78;packet[5]=0x56;packet[6]=0x34;packet[7]=0x12;
    packet[8]=0xef;packet[9]=0xcd;packet[10]=0xab;packet[11]=0x90;packet[16]=0x45;packet[17]=0x23;
    for(size_t size=0;size<20;++size)queue.receive(std::span(packet).first(size),0,apply);
    require(queue.pending()==0&&seen.empty(),"truncated packet entered pending queue");
    queue.receive(packet,0,apply);queue.ready(0,apply);
    require(queue.pending()==1&&seen.empty(),"early ownership applied before hero known");
    queue.ready(0x12345678,apply);
    require(queue.pending()==0&&seen.size()==1,"held ownership was not applied once");
    require(seen[0].guid==0x90abcdef&&seen[0].classId==0x123&&seen[0].name==0x2345&&seen[0].type==1,"packet field decoding changed");
    queue.ready(0x12345678,apply);require(seen.size()==1,"held packet applied twice");
    queue.receive(packet,0x12345679,apply);require(seen.size()==1,"foreign owner's hireling accepted");
    queue.receive(packet,0,apply);queue.ready(0x12345679,apply);
    require(queue.pending()==0&&seen.size()==1,"foreign pending packet survived owner check");
    queue.receive(packet,0,apply);queue.clear();queue.ready(0x12345678,apply);
    require(seen.size()==1&&queue.pending()==0,"old pending packet crossed session reset with reused hero GUID");
    queue.receive(packet,0x12345678,apply);require(seen.size()==2,"current session ownership lost after reset");
    packet[0]=0x80;queue.receive(packet,0,apply);require(queue.pending()==0,"wrong packet type queued");
    std::cout<<"PASS: early/late ownership, exact decoding, truncated packets, foreign owners, once-only drain and reset with reused hero GUID\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
