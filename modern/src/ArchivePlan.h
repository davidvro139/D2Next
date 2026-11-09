#pragma once
#include "ProgramPath.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <vector>

namespace d2 {
// Startup/session consistency, not a cryptographic content seal. Equal size and
// restored timestamps can hide byte changes; immutable/hash-verified mod profiles
// remain a separate loader requirement. No archive payload is read here.
struct ArchiveEntryIdentity {
    std::filesystem::path relativePath;
    uintmax_t bytes{};
    std::filesystem::file_time_type modified{};
    bool operator==(const ArchiveEntryIdentity&) const=default;
};
inline std::vector<ArchiveEntryIdentity> archivePlanIdentity(const std::filesystem::path& root,
        const std::vector<std::filesystem::path>& paths) {
    namespace fs=std::filesystem;
    std::vector<ArchiveEntryIdentity> result;
    for(const auto& path:paths) {
        const auto relative=path.lexically_normal().lexically_relative(root.lexically_normal());
        if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")
            throw std::runtime_error("Archive is outside game directory: "+path.string());
        std::error_code error;
        const auto bytes=fs::file_size(path,error);
        if(error)throw std::runtime_error("Cannot inspect archive "+path.string()+": "+error.message());
        const auto modified=fs::last_write_time(path,error);
        if(error)throw std::runtime_error("Cannot inspect archive timestamp "+path.string()+": "+error.message());
        result.push_back({relative,bytes,modified});
    }
    return result;
}
// Highest precedence first. Both the renderer and D2Common must use this plan.
// Mod selection must eventually extend this plan, never just one consumer.
inline std::vector<std::filesystem::path> vanillaArchivePlan(const std::filesystem::path& root) {
    auto exists=[](const std::filesystem::path& path) {
        std::error_code error;
        const bool found=std::filesystem::exists(path,error);
        if(error)throw std::runtime_error("Cannot access game file "+path.string()+": "+error.message());
        return found;
    };
    const auto core=gameFile(root,"D2SE/CORES/1.10f/Patch_D2.mpq");
    const auto patch=gameFile(root,"Patch_D2.mpq");
    if(!exists(gameFile(root,"d2data.mpq")))
        throw std::runtime_error("Game directory must contain d2data.mpq");
    if(!exists(core)&&!exists(patch))
        throw std::runtime_error("Missing Diablo II 1.10f Patch_D2.mpq in "+root.string());
    std::vector<std::filesystem::path> paths{exists(core)?core:patch};
    for(const auto* name:{"d2exp.mpq","d2data.mpq","d2char.mpq","d2sfx.mpq",
        "d2xtalk.mpq","d2speech.mpq","D2XVIDEO.MPQ","d2video.mpq","d2xmusic.mpq","d2music.mpq"}) {
        const auto path=gameFile(root,name);
        if(exists(path))paths.push_back(path);
    }
    return paths;
}
// A mod: the MPQs in <game>/Mods/<name>/, opened before the vanilla ones (as Patch_D2.mpq is before d2data.mpq), so a
// file they hold replaces the game's. Among themselves by file name, A before B (name them 1-..., 2-... to order them).
// An empty name is vanilla. The name is one folder: no separators, no "." or "..".
inline std::vector<std::filesystem::path> archivePlan(const std::filesystem::path& root,const std::string& mod) {
    namespace fs=std::filesystem;
    auto paths=vanillaArchivePlan(root);
    if(mod.empty())return paths;
    if(mod=="."||mod==".."||mod.find_first_of("/\\:")!=std::string::npos)throw std::runtime_error("Invalid mod name: "+mod);
    const auto folder=root/"Mods"/fs::path(mod);
    std::error_code error;
    if(!fs::is_directory(folder,error))throw std::runtime_error("Mod folder not found: "+folder.string());
    std::vector<fs::path> mpqs;
    for(const auto& entry:fs::directory_iterator(folder,error)) {
        auto extension=entry.path().extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(entry.is_regular_file(error)&&extension==".mpq")mpqs.push_back(entry.path());
    }
    if(error)throw std::runtime_error("Cannot list mod folder "+folder.string()+": "+error.message());
    if(mpqs.empty())throw std::runtime_error("Mod folder has no MPQ: "+folder.string());
    std::sort(mpqs.begin(),mpqs.end(),[](const fs::path& a,const fs::path& b){return a.filename().string()<b.filename().string();});
    paths.insert(paths.begin(),mpqs.begin(),mpqs.end());
    return paths;
}
// The mod this process plays (D2NEXT_MOD, a folder name in <game>/Mods; unset or empty: vanilla). The renderer, the
// game server (D2Common) and the x64 helper all build their plan from it, so they see the same files.
inline std::string activeMod() {const char* name=std::getenv("D2NEXT_MOD");return name?std::string(name):std::string();}
inline std::vector<std::filesystem::path> archivePlan(const std::filesystem::path& root) {return archivePlan(root,activeMod());}
// A mod's characters are kept apart from vanilla ones (their items and stats may not exist in the other's tables):
// <saves>/Mods/<name>.
inline std::filesystem::path modSaveDirectory(const std::filesystem::path& saves,const std::string& mod=activeMod()) {
    return mod.empty()?saves:saves/"Mods"/std::filesystem::path(mod);
}
// Storm searches larger priorities first; no ties, including optional archives.
inline int archivePriority(size_t index) { return 100000-static_cast<int>(index); }
}
