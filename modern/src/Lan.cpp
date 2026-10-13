#include "Lan.h"
#include "NativeSession.h"
#include "Backend.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket=SOCKET;
static const Socket noSocket=INVALID_SOCKET;
static void closeSocket(Socket s){closesocket(s);}
static bool wouldBlock(){return WSAGetLastError()==WSAEWOULDBLOCK;}
static void setNonBlocking(Socket s){u_long on=1;ioctlsocket(s,FIONBIO,&on);}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int;
static const Socket noSocket=-1;
static void closeSocket(Socket s){close(s);}
static bool wouldBlock(){return errno==EWOULDBLOCK||errno==EAGAIN;}
static void setNonBlocking(Socket s){fcntl(s,F_SETFL,fcntl(s,F_GETFL,0)|O_NONBLOCK);}
#endif
namespace d2::lan {
namespace {
constexpr char hello[4]={'D','2','L','2'}; // "D2LN" before snapshots went as changes
constexpr uint32_t frameReply=1,frameSave=2;
// Winsock needs starting once per process.
void startNetwork() {
#ifdef _WIN32
    static bool started=false;
    if(!started){WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("Cannot start Winsock");started=true;}
#endif
}
void noDelay(Socket s){int on=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&on),sizeof(on));}
// Sends all of it (the socket may be non-blocking: waits out a full buffer).
bool sendAll(Socket s,const void* data,size_t size) {
    const char* at=static_cast<const char*>(data);
    while(size) {
        const int n=::send(s,at,int(std::min<size_t>(size,1<<20)),0);
        if(n>0){at+=n;size-=size_t(n);continue;}
        if(n<0&&wouldBlock()){fd_set w;FD_ZERO(&w);FD_SET(s,&w);timeval t{1,0};select(int(s)+1,nullptr,&w,nullptr,&t);continue;}
        return false;
    }
    return true;
}
// Receives exactly size bytes (blocking socket); with a timeout in milliseconds (0: none), false when it runs out.
bool receiveAll(Socket s,void* data,size_t size,int timeoutMs=0) {
    char* at=static_cast<char*>(data);
    while(size) {
        if(timeoutMs>0){fd_set r;FD_ZERO(&r);FD_SET(s,&r);timeval t{timeoutMs/1000,(timeoutMs%1000)*1000};if(select(int(s)+1,&r,nullptr,nullptr,&t)<=0)return false;}
        const int n=::recv(s,at,int(std::min<size_t>(size,1<<20)),0);if(n<=0)return false;at+=n;size-=size_t(n);
    }
    return true;
}
std::string saveName(const std::vector<uint8_t>& save){return std::string(reinterpret_cast<const char*>(save.data()+0x14));}
// Snapshots go as the change from the one before (TCP keeps them in order): the bytes XORed with the previous snapshot's,
// as runs of unchanged bytes and runs of changed ones, each length a varint, the changed bytes after their length.
// Most of the 72 KB snapshot stays the same from tick to tick: standing in town a guest took 73 bytes a snapshot (2 KB a
// second) instead of 1.8 MB a second (2026-10-04, the first snapshot, from nothing, included).
void putVarint(std::vector<uint8_t>& out,uint32_t value){while(value>=0x80){out.push_back(uint8_t(value|0x80));value>>=7;}out.push_back(uint8_t(value));}
bool getVarint(const uint8_t*& at,const uint8_t* end,uint32_t& value) {
    value=0;
    for(int shift=0;shift<35;shift+=7){if(at==end)return false;const uint8_t b=*at++;value|=uint32_t(b&0x7F)<<shift;if(!(b&0x80))return true;}
    return false;
}
void encodeChange(const uint8_t* before,const uint8_t* now,size_t size,std::vector<uint8_t>& out) {
    size_t at=0;
    while(at<size) {
        const size_t same=at;
        while(at<size&&before[at]==now[at])++at;
        // The changed run ends at 8 unchanged bytes in a row (shorter gaps cost less to send as changed bytes).
        const size_t changed=at;size_t quiet=0;
        while(at<size&&quiet<8){quiet=before[at]==now[at]?quiet+1:0;++at;}
        if(quiet==8)at-=8;else at-=quiet;
        putVarint(out,uint32_t(changed-same));putVarint(out,uint32_t(at-changed));
        for(size_t i=changed;i<at;++i)out.push_back(uint8_t(before[i]^now[i]));
    }
}
bool decodeChange(uint8_t* snapshot,size_t size,const uint8_t* at,const uint8_t* end) {
    size_t position=0;
    while(at<end) {
        uint32_t same=0,changed=0;
        if(!getVarint(at,end,same)||!getVarint(at,end,changed))return false;
        if(position+same+changed>size||size_t(end-at)<changed)return false;
        position+=same;
        for(uint32_t i=0;i<changed;++i)snapshot[position++]^=*at++;
    }
    return true;
}
// "host" and "port" of "host[:port]" (the default port when none is given).
std::pair<std::string,std::string> splitAddress(const std::string& address) {
    if(const auto colon=address.rfind(':');colon!=std::string::npos)return {address.substr(0,colon),address.substr(colon+1)};
    return {address,std::to_string(defaultPort)};
}
}
std::string localAddress() {
    try{startNetwork();}catch(const std::exception&){return {};}
    // The address of the network card that carries this computer's traffic: a UDP socket "connected" to an outside
    // address (nothing is sent) is given that card's address. Virtual adapters (VPNs, virtual machines) come first in
    // the computer's own list, so that list is only the fallback, for a computer with no route out.
    if(const Socket probe=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);probe!=noSocket) {
        sockaddr_in outside{};outside.sin_family=AF_INET;outside.sin_port=htons(53);
        inet_pton(AF_INET,"8.8.8.8",&outside.sin_addr);
        sockaddr_in self{};socklen_t size=sizeof(self);
        const bool found=connect(probe,reinterpret_cast<const sockaddr*>(&outside),sizeof(outside))==0&&
            getsockname(probe,reinterpret_cast<sockaddr*>(&self),&size)==0&&self.sin_addr.s_addr!=0;
        closeSocket(probe);
        char text[INET_ADDRSTRLEN]{};
        if(found&&inet_ntop(AF_INET,&self.sin_addr,text,sizeof(text)))return text;
    }
    char name[256]{};
    if(gethostname(name,sizeof(name)-1)!=0)return {};
    addrinfo hints{};hints.ai_family=AF_INET;addrinfo* found=nullptr;
    if(getaddrinfo(name,nullptr,&hints,&found)!=0)return {};
    std::string result;
    for(auto* at=found;at&&result.empty();at=at->ai_next) {
        const auto* ip=reinterpret_cast<const sockaddr_in*>(at->ai_addr);
        const uint32_t value=ntohl(ip->sin_addr.s_addr);
        if((value>>24)==127||!value)continue;
        char text[INET_ADDRSTRLEN]{};
        if(inet_ntop(AF_INET,&ip->sin_addr,text,sizeof(text)))result=text;
    }
    freeaddrinfo(found);
    return result;
}
bool validAddress(const std::string& address) {
    const auto [host,port]=splitAddress(address);
    if(host.empty()||port.empty()||!std::all_of(port.begin(),port.end(),[](char c){return c>='0'&&c<='9';}))return false;
    try{startNetwork();}catch(const std::exception&){return false;}
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;addrinfo* found=nullptr;
    if(getaddrinfo(host.c_str(),port.c_str(),&hints,&found)!=0||!found)return false;
    freeaddrinfo(found);
    return true;
}
struct Host::Impl {
    struct Guest {
        Socket socket=noSocket;std::vector<uint8_t> inbox;int32_t client=0;std::string name;
        bool gone=false;      // closed: removed after this tick
        int leaving=-1;       // ticks since its Quit while the host waits for the game to save the character (-1: playing)
        bool closing=false;   // its saved character is queued: closed once it has gone out
        // What is still to go to it (sent as its connection takes it, so a guest that stops reading never stops the host),
        // how much of that has gone, the ticks it has taken nothing, and the snapshot it was sent last.
        std::vector<uint8_t> outbox;size_t sent=0;int stalled=0;
        std::vector<uint8_t> last=std::vector<uint8_t>(sizeof(native::Reply),0);
    };
    Socket listener=noSocket;
    std::vector<Guest> guests;
    std::unique_ptr<native::Reply> reply=std::make_unique<native::Reply>();
    std::vector<uint8_t> change;
    static void queue(Guest& g,const void* data,size_t size){const auto* b=static_cast<const uint8_t*>(data);g.outbox.insert(g.outbox.end(),b,b+size);}
    static void queueFrame(Guest& g,uint32_t kind,const void* data,uint32_t size){const uint32_t header[2]={kind,size};queue(g,header,sizeof(header));queue(g,data,size);}
    // The snapshot for a guest, as its change from the last one it was sent.
    void queueSnapshot(Guest& g) {
        const auto* now=reinterpret_cast<const uint8_t*>(reply.get());
        change.clear();encodeChange(g.last.data(),now,sizeof(native::Reply),change);
        std::memcpy(g.last.data(),now,sizeof(native::Reply));
        queueFrame(g,frameReply,change.data(),uint32_t(change.size()));
    }
    // Sends what the connection takes now; false when it has closed.
    static bool flush(Guest& g) {
        bool moved=false;
        while(g.sent<g.outbox.size()) {
            const int n=::send(g.socket,reinterpret_cast<const char*>(g.outbox.data()+g.sent),int(std::min<size_t>(g.outbox.size()-g.sent,1<<20)),0);
            if(n>0){g.sent+=size_t(n);moved=true;continue;}
            if(n<0&&wouldBlock())break;
            return false;
        }
        if(g.sent==g.outbox.size()){g.outbox.clear();g.sent=0;}
        else if(g.sent>(1u<<20)){g.outbox.erase(g.outbox.begin(),g.outbox.begin()+std::ptrdiff_t(g.sent));g.sent=0;}
        g.stalled=g.outbox.empty()||moved?0:g.stalled+1;
        return true;
    }
};
Host::Host(uint16_t port):impl_(std::make_unique<Impl>()) {
    startNetwork();
    impl_->listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(impl_->listener==noSocket)throw std::runtime_error("Cannot open the LAN socket");
    int reuse=1;setsockopt(impl_->listener,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&reuse),sizeof(reuse));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);address.sin_port=htons(port);
    if(bind(impl_->listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||listen(impl_->listener,8)!=0)
        throw std::runtime_error("Cannot listen for LAN players on port "+std::to_string(port));
    setNonBlocking(impl_->listener);
    std::printf("LAN: hosting on port %u\n",unsigned(port));std::fflush(stdout);
}
Host::~Host() {
    for(auto& g:impl_->guests)if(g.socket!=noSocket)closeSocket(g.socket);
    if(impl_->listener!=noSocket)closeSocket(impl_->listener);
}
size_t Host::guests() const {size_t n=0;for(const auto& g:impl_->guests)n+=g.client&&!g.gone&&g.leaving<0;return n;}
void Host::beforeTick(native::Session& session) {
    // New connections.
    for(;;) {
        const Socket s=accept(impl_->listener,nullptr,nullptr);
        if(s==noSocket)break;
        setNonBlocking(s);noDelay(s);
        Impl::Guest g;g.socket=s;impl_->guests.push_back(std::move(g));
        std::printf("LAN: a player is connecting\n");std::fflush(stdout);
    }
    for(auto& g:impl_->guests) {
        if(g.gone||g.leaving>=0)continue;
        // Whatever has arrived.
        char buffer[65536];bool closed=false;
        for(;;) {
            const int n=recv(g.socket,buffer,sizeof(buffer),0);
            if(n>0){g.inbox.insert(g.inbox.end(),buffer,buffer+n);continue;}
            if(n<0&&wouldBlock())break;
            closed=true;break;
        }
        if(!g.client) {
            if(closed){g.gone=true;continue;}
            // The hello: "D2L2", magic, save size, save.
            if(g.inbox.size()<12)continue;
            uint32_t magic=0,size=0;std::memcpy(&magic,g.inbox.data()+4,4);std::memcpy(&size,g.inbox.data()+8,4);
            if(std::memcmp(g.inbox.data(),hello,4)!=0||magic!=native::magic||size<0x30||size>65535){uint32_t no=0;sendAll(g.socket,&no,4);g.gone=true;continue;}
            if(g.inbox.size()<12+size)continue;
            std::vector<uint8_t> save(g.inbox.begin()+12,g.inbox.begin()+12+size);
            g.inbox.erase(g.inbox.begin(),g.inbox.begin()+12+size);
            try {
                g.client=session.join(save);g.name=saveName(save);
                const uint32_t yes=uint32_t(g.client);
                session.describeClient(*impl_->reply,g.client); // the snapshot the guest starts from
                Impl::queue(g,&yes,4);impl_->queueSnapshot(g);
                if(!Impl::flush(g)){g.gone=true;session.leaveClient(g.client);}
                std::printf("LAN: %s joined as client %d\n",g.name.c_str(),int(g.client));std::fflush(stdout);
            } catch(const std::exception& e) {
                std::printf("LAN: could not take the player in: %s\n",e.what());std::fflush(stdout);
                uint32_t no=0;sendAll(g.socket,&no,4);g.gone=true;
            }
            continue;
        }
        // Requests.
        while(g.inbox.size()>=sizeof(native::Request)&&g.leaving<0) {
            native::Request request;std::memcpy(&request,g.inbox.data(),sizeof(request));
            g.inbox.erase(g.inbox.begin(),g.inbox.begin()+sizeof(request));
            if(request.operation==native::Operation::Quit) {
                // Leaving: the game saves the character as it takes it out; it goes back to the guest after this tick.
                session.leaveClient(g.client);g.leaving=0;
                std::printf("LAN: %s is leaving\n",g.name.c_str());std::fflush(stdout);
                break;
            }
            try{session.apply(request,g.client);}
            catch(const std::exception& e){std::printf("LAN: bad request from client %d: %s\n",int(g.client),e.what());closed=true;break;}
        }
        if(closed&&g.leaving<0) {
            // Gone without leaving (closed, crashed, cable out): the character leaves the game; its save can't be sent.
            session.leaveClient(g.client);g.gone=true;
            std::printf("LAN: %s dropped out\n",g.name.c_str());std::fflush(stdout);
        }
    }
}
void Host::afterTick(native::Session& session) {
    // A guest that has taken nothing for 10 seconds (a phone put away, a frozen game) is let go, so the others play on.
    constexpr int stallLimit=250;
    for(auto& g:impl_->guests) {
        if(g.gone||!g.client)continue;
        if(g.closing) {
            // Its saved character is on its way; the link closes once it has gone.
            if(!Impl::flush(g)||g.outbox.empty()||g.stalled>stallLimit)g.gone=true;
            continue;
        }
        if(g.leaving>=0) {
            // Waiting for the game to save the leaving character (a few ticks), then it goes back and the link closes.
            std::vector<uint8_t> save;
            const bool saved=session.takeGuestSave(g.name,save);
            if(saved||++g.leaving>125) {
                Impl::queueFrame(g,frameSave,save.data(),uint32_t(save.size()));g.closing=true;
                std::printf("LAN: %s left%s\n",g.name.c_str(),saved?" (character sent back)":" (the game saved nothing)");std::fflush(stdout);
                if(!Impl::flush(g)||g.outbox.empty())g.gone=true;
            }
            continue;
        }
        session.describeClient(*impl_->reply,g.client);
        impl_->queueSnapshot(g);
        if(!Impl::flush(g)||g.stalled>stallLimit) {
            g.gone=true;session.leaveClient(g.client);
            std::printf("LAN: %s dropped out%s\n",g.name.c_str(),g.stalled>stallLimit?" (took nothing for 10 seconds)":"");std::fflush(stdout);
        }
    }
    for(auto it=impl_->guests.begin();it!=impl_->guests.end();)
        if(it->gone){closeSocket(it->socket);it=impl_->guests.erase(it);}else ++it;
}
struct Guest::Impl {
    Socket socket=noSocket;int32_t client=0;bool left=false,lost=false;
    uint64_t snapshots=0,bytes=0; // taken in so far (reported as the guest leaves)
    std::vector<uint8_t> last=std::vector<uint8_t>(sizeof(native::Reply),0); // the snapshot the changes apply to
    // What has come in: the bytes of a frame still arriving, the whole snapshots not yet played (as changes), the
    // saved character once it has come, and when the last snapshot arrived.
    std::vector<uint8_t> inbox;
    std::deque<std::vector<uint8_t>> frames;
    std::optional<std::vector<uint8_t>> save;
    std::chrono::steady_clock::time_point lastArrival=std::chrono::steady_clock::now();
    // Lost: the host is gone (or was never there); the game goes back to the menus with this.
    [[noreturn]] void fail(const std::string& why){lost=true;throw ConnectionLost(why);}
    // Waits (calling `waiting` every 50 ms, which keeps the loading screen drawn) until the socket can be read or
    // written, at most timeoutMs.
    bool await(bool write,int timeoutMs,const std::function<void()>& waiting) const {
        for(int waited=0;waited<timeoutMs;waited+=50) {
            fd_set set;FD_ZERO(&set);FD_SET(socket,&set);timeval t{0,50000};
            const int n=write?select(int(socket)+1,nullptr,&set,nullptr,&t):select(int(socket)+1,&set,nullptr,nullptr,&t);
            if(n>0)return true;
            if(n<0)return false;
            if(waiting)waiting();
        }
        return false;
    }
    // Takes in whatever has arrived, never waiting (the socket is non-blocking), and splits it into frames.
    void poll() {
        char buffer[65536];bool closed=false;
        for(;;) {
            const int n=::recv(socket,buffer,int(sizeof(buffer)),0);
            if(n>0){inbox.insert(inbox.end(),buffer,buffer+n);continue;}
            if(n<0&&wouldBlock())break;
            closed=true;break;
        }
        size_t at=0;
        while(inbox.size()-at>=8) {
            uint32_t header[2];std::memcpy(header,inbox.data()+at,8);
            if(header[1]>(1u<<24)||(header[0]!=frameReply&&header[0]!=frameSave))fail("The LAN host sent something unexpected");
            if(inbox.size()-at<8+size_t(header[1]))break;
            std::vector<uint8_t> payload(inbox.begin()+std::ptrdiff_t(at+8),inbox.begin()+std::ptrdiff_t(at+8+header[1]));
            if(header[0]==frameSave)save=std::move(payload);
            else{frames.push_back(std::move(payload));lastArrival=std::chrono::steady_clock::now();}
            at+=8+header[1];
        }
        inbox.erase(inbox.begin(),inbox.begin()+std::ptrdiff_t(at));
        if(closed&&!save)fail("The connection to the host was lost");
    }
};
Guest::Guest(const std::string& address,const std::vector<uint8_t>& save,const std::function<void()>& waiting):impl_(std::make_unique<Impl>()) {
    startNetwork();
    const auto [host,port]=splitAddress(address);
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;addrinfo* found=nullptr;
    if(getaddrinfo(host.c_str(),port.c_str(),&hints,&found)!=0||!found)impl_->fail("Cannot find the LAN host "+address);
    impl_->socket=socket(found->ai_family,found->ai_socktype,found->ai_protocol);
    if(impl_->socket==noSocket){freeaddrinfo(found);impl_->fail("Cannot open a socket");}
    // Connecting in the background (at most 10 seconds) while the loading screen stays drawn: a phone that draws
    // nothing for 5 seconds is reported as not responding. The socket stays non-blocking: the game never waits on it.
    setNonBlocking(impl_->socket);
    const int started=connect(impl_->socket,found->ai_addr,int(found->ai_addrlen));
    // Read before anything else runs (freeing the address list can overwrite the error).
#ifdef _WIN32
    const bool inProgress=started!=0&&WSAGetLastError()==WSAEWOULDBLOCK;
#else
    const bool inProgress=started!=0&&errno==EINPROGRESS;
#endif
    freeaddrinfo(found);
    if(started!=0&&!inProgress)impl_->fail("Cannot connect to the LAN host "+address);
    if(inProgress) {
        int error=0;socklen_t size=sizeof(error);
        if(!impl_->await(true,10000,waiting)||getsockopt(impl_->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)!=0||error)
            impl_->fail("Cannot connect to the LAN host "+address);
    }
    noDelay(impl_->socket);
    const uint32_t magic=native::magic,size=uint32_t(save.size());
    if(!sendAll(impl_->socket,hello,4)||!sendAll(impl_->socket,&magic,4)||!sendAll(impl_->socket,&size,4)||!sendAll(impl_->socket,save.data(),save.size()))
        impl_->fail("The LAN host closed the connection");
    // The host answers on its next tick, then sends the first snapshot; 20 seconds is a host that is not playing.
    uint32_t client=0;
    if(!impl_->await(false,20000,waiting)||!receiveAll(impl_->socket,&client,4,1000)||!client)
        impl_->fail("The LAN host did not take the character in (another version, or a bad save)");
    impl_->client=int32_t(client);
    const auto asked=std::chrono::steady_clock::now();
    for(impl_->poll();impl_->frames.empty();impl_->poll()) {
        if(std::chrono::steady_clock::now()-asked>std::chrono::seconds(20))impl_->fail("The LAN host sent no game");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(waiting)waiting();
    }
}
Guest::~Guest(){if(impl_->socket!=noSocket)closeSocket(impl_->socket);}
void Guest::send(const native::Request& request){if(!sendAll(impl_->socket,&request,sizeof(request)))impl_->fail("The connection to the host was lost");}
void Guest::poll(){impl_->poll();}
size_t Guest::buffered() const {return impl_->frames.size();}
double Guest::quietSeconds() const {return std::chrono::duration<double>(std::chrono::steady_clock::now()-impl_->lastArrival).count();}
void Guest::next(native::Reply& reply) {
    if(impl_->frames.empty())throw std::logic_error("No LAN snapshot to play");
    const auto& change=impl_->frames.front();
    if(!decodeChange(impl_->last.data(),impl_->last.size(),change.data(),change.data()+change.size()))
        impl_->fail("The LAN host sent an invalid snapshot");
    impl_->snapshots+=1;impl_->bytes+=8+change.size();
    impl_->frames.pop_front();
    std::memcpy(&reply,impl_->last.data(),sizeof(reply));
    if(reply.signature!=native::magic)impl_->fail("The LAN host sent an invalid snapshot");
}
void Guest::receive(native::Reply& reply) {
    // The host sends a snapshot every tick: 4 seconds without one is a host that is gone (and stays under the 5 seconds
    // after which a phone reports the game as not responding).
    const auto asked=std::chrono::steady_clock::now();
    for(impl_->poll();impl_->frames.empty();impl_->poll()) {
        if(std::chrono::steady_clock::now()-asked>std::chrono::seconds(4))impl_->fail("The connection to the host was lost");
        impl_->await(false,50,{});
    }
    next(reply);
}
int32_t Guest::client() const {return impl_->client;}
bool Guest::leave(const std::filesystem::path& saveOut) {
    if(impl_->left||impl_->lost)return false;
    impl_->left=true;
    if(impl_->snapshots)std::printf("LAN: took in %llu snapshots, %llu bytes (%llu a snapshot)\n",(unsigned long long)impl_->snapshots,
        (unsigned long long)impl_->bytes,(unsigned long long)(impl_->bytes/impl_->snapshots));
    const native::Request quit{native::magic,native::Operation::Quit};
    if(!sendAll(impl_->socket,&quit,sizeof(quit)))return false;
    // Snapshots still on their way come first; then the saved character (a few ticks, at most 4 seconds).
    const auto asked=std::chrono::steady_clock::now();
    try {
        for(impl_->poll();!impl_->save;impl_->poll()) {
            if(std::chrono::steady_clock::now()-asked>std::chrono::seconds(4))return false;
            impl_->await(false,50,{});
        }
    } catch(const ConnectionLost&) {return false;}
    const auto& payload=*impl_->save;
    if(payload.empty()||saveOut.empty())return !payload.empty();
    // Written beside it first and then moved over it, so a failure leaves the old save whole.
    auto temporary=saveOut;temporary+=".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(payload.data()),std::streamsize(payload.size()));if(!out)return false;}
    std::error_code error;std::filesystem::rename(temporary,saveOut,error);
    if(error){std::filesystem::remove(saveOut,error);std::filesystem::rename(temporary,saveOut,error);}
    std::printf("LAN: character saved to %s\n",saveOut.string().c_str());std::fflush(stdout);
    return !error;
}
}
