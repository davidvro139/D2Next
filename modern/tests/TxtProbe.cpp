// Prints chosen columns of a data\global\excel table, one row per line (tab-separated, with the row number first), for
// rows whose first chosen column is not empty: e.g. TxtProbe <game dir> states.txt gfxtype gfxclass state
// (the "Expansion" divider rows are dropped, as TextTable does everywhere).
#include "Assets.h"
#include "Tables.h"
#include <iostream>
int main(int argc,char** argv){try {
    if(argc<4)return 2;
    d2::Archives archives(argv[1]);
    const auto bytes=archives.read(std::string("data\\global\\excel\\")+argv[2]);
    const d2::TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    for(size_t r=0;r<table.rows();++r) {
        if(table.cell(r,argv[3]).empty())continue;
        std::cout<<r;
        for(int c=3;c<argc;++c)std::cout<<'\t'<<table.cell(r,argv[c]);
        std::cout<<'\n';
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
