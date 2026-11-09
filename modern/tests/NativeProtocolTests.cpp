#include "NativeProtocol.h"
#include <iostream>
#include <stdexcept>
int main(){try{
    using namespace d2::native;
    auto require=[](bool ok){if(!ok)throw std::runtime_error("native handshake regression");};
    const Hello local;require(compatible(local));
    auto bad=local;bad.signature=magic;require(!compatible(bad)); // legacy Reply prefix
    bad=local;--bad.version;require(!compatible(bad));
    bad=local;++bad.version;require(!compatible(bad));
    bad=local;--bad.requestBytes;require(!compatible(bad));
    bad=local;++bad.replyBytes;require(!compatible(bad));
    bad={0,0,0,0};require(!compatible(bad));
    std::cout<<"PASS helper handshake rejects legacy signature, versions and message-size mismatches\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
