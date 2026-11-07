#pragma once
#include <LooseFiles.h>
#include "ArchivePlan.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace d2 {
using Bytes = std::vector<uint8_t>;
using Palette = std::array<std::array<uint8_t, 4>, 256>;
// A picture: rgba, 4 bytes a pixel, or, for a compacted one (see compactAnimation), one byte a pixel into colors.
struct Image {
    uint32_t width{}, height{}; Bytes rgba;
    Bytes indices; std::shared_ptr<const std::vector<std::array<uint8_t,4>>> colors;
    bool hasPixels() const {return !rgba.empty()||!indices.empty();}
    uint8_t alpha(size_t pixel) const {return indices.empty()?rgba[pixel*4+3]:(*colors)[indices[pixel]][3];}
    std::array<uint8_t,4> color(size_t pixel) const {
        if(!indices.empty())return (*colors)[indices[pixel]];
        return {rgba[pixel*4],rgba[pixel*4+1],rgba[pixel*4+2],rgba[pixel*4+3]};
    }
    // The pixels as rgba, for a GPU upload.
    Bytes expanded() const;
    size_t storedBytes() const {return rgba.capacity()+indices.capacity();}
};
Palette decodePalette(std::span<const uint8_t> bytes);
Image decodeDc6(std::span<const uint8_t> bytes, const Palette& palette, uint32_t frame = 0);
uint32_t dc6FrameCount(std::span<const uint8_t> bytes);
// Where a DC6 frame is drawn relative to its anchor: left edge x, and y of its bottom row (the frame extends up from there).
std::array<int32_t,2> dc6FrameOffset(std::span<const uint8_t> bytes,uint32_t frame);
// glow: the frame's see-through layers (same size and place as image, empty if none), drawn over it with glowEffect: the COF
// draw effect, 0/1/2 = 75%/50%/25% opaque, 3 and 6 = added onto the scene; 255 none.
// shadow: what of the frame casts a shadow on the ground (COF layer shadow flag): 0 nothing, 1 all of image, 2 the pixels
// of shadowMask (same size and place as image).
struct AnimationFrame {Image image;int x{},y{};Image glow;uint8_t glowEffect=255;uint8_t shadow=0;Image shadowMask;};
struct Animation {uint32_t directions{},framesPerDirection{};std::vector<AnimationFrame> frames;};
// Stores the animation's pictures (image, glow, shadow mask) one byte a pixel, with one colour table for all of them,
// when they hold at most 256 different rgba values together; otherwise leaves them as they are. Drawn the same either way.
void compactAnimation(Animation& animation);
Animation decodeDcc(std::span<const uint8_t> bytes,const Palette& palette);
Animation decodeDc6Animation(std::span<const uint8_t> bytes,const Palette& palette);
// The file's direction for our direction d of `dirs` (clockwise from screen-down). Files store the four diagonals first,
// then the four straight directions, then each finer set of in-between directions.
unsigned fileDirection(unsigned dirs,unsigned d);
// The animation with its directions put in our order (as loadCharacter already does for characters).
Animation inScreenOrder(Animation animation);
// DT1 collision bits that stop the player: COLLIDE_WALL | COLLIDE_NOPLAYER.
inline constexpr uint8_t collideSolid=0x09;
struct Tile {
    Image image;
    int32_t originX{}, originY{};
    uint32_t orientation{}, mainIndex{}, subIndex{}, rarity{};
    int32_t roofHeight{}; // DT1 header: how far a roof tile (orientation 15) is raised above the ground
    bool animated=false;  // DT1 header byte 7: one frame of an animation (the tiles with the same orientation, main and sub index)
    uint8_t material=0;   // DT1 header byte 6 (D2CMP's material flags): 2 water, where D2Client splashes rain and bubbles mud
    std::array<uint8_t,25> collision{};
};
inline bool isFloorExtension(const Tile& tile) {
    return tile.orientation>=16&&tile.orientation<=19;
}
inline bool tileDrawsWithGround(const Tile& tile) { return tile.orientation==0||isFloorExtension(tile); }
uint32_t dt1TileCount(std::span<const uint8_t> bytes);
Tile decodeDt1(std::span<const uint8_t> bytes, const Palette& palette, uint32_t tile);
Image dt1Atlas(std::span<const uint8_t> bytes, const Palette& palette, uint32_t page);
inline constexpr uint32_t tilesPerPage=16;
struct MapCell { uint32_t value{}, orientation{}; };
struct MapObject { uint32_t type{}, id{}, x{}, y{}, flags{}; }; // a DS1 preset unit (type 1 monster, 2 object)
struct MapLayout {
    uint32_t version{}, width{}, height{}, act{}, objects{};
    std::vector<std::string> dependencies;
    std::vector<std::vector<MapCell>> floors,walls;
    std::vector<MapCell> shadows;
    std::vector<MapObject> objectList; // the preset monsters and objects
};
MapLayout decodeDs1(std::span<const uint8_t> bytes);
class Archives {
public:
    explicit Archives(const std::filesystem::path& root);
    ~Archives();
    Archives(const Archives&) = delete;
    Archives& operator=(const Archives&) = delete;
    Bytes read(const std::string& name,size_t maxSize=64*1024*1024) const;
    // A file read in pieces (music, streamed while it plays); use it on the thread that owns these archives.
    class Piecewise {
    public:
        ~Piecewise();
        uint32_t size() const {return size_;}
        Bytes read(uint32_t offset,uint32_t length);
    private:
        friend class Archives;
        void* file_=nullptr;uint32_t size_=0;
    };
    std::unique_ptr<Piecewise> open(const std::string& name) const; // null when no archive has it
    size_t count() const { return handles_.size(); }
    void validateCurrent() const; // reject a changed plan/file metadata before reuse
    // The game folder the archives were opened from (another thread can open its own copy: StormLib handles are not shared).
    const std::filesystem::path& root() const { return root_; }
private:
    std::vector<void*> handles_;
    std::filesystem::path root_;
    std::vector<ArchiveEntryIdentity> identity_;
    d2loose::Folder loose_; // the active mod's data folder, read before the MPQs
};
// One thread that reads and decodes game files for several owners (object, missile and item art, sounds), with its own
// Archives. work runs on that thread and returns what to do with the result; pump(owner) runs the owner's finished ones on
// the caller's thread. A first read of a file can wait tens of ms while the phone frees memory: not in the frame.
class BackgroundReads {
public:
    using Done=std::function<void()>;
    using Work=std::function<Done(const Archives&)>;
    static std::shared_ptr<BackgroundReads> shared(const std::filesystem::path& root);
    explicit BackgroundReads(std::filesystem::path root);
    ~BackgroundReads();
    void queue(const void* owner,Work work);
    void pump(const void* owner);
    // Drops the owner's queued and finished work and waits for its running job: call before the owner dies, so that no
    // work of it reaches another object later made at the same address.
    void forget(const void* owner);
private:
    struct State;
    std::unique_ptr<State> state_;
};
Animation loadCharacter(const Archives& archives,const Palette& palette,const std::string& mode);
struct CharacterAppearance {
    std::string token="so",weaponClass="hth";
    std::array<std::string,16> components{"lit","lit","lit","lit","lit","","","","lit","lit","lit","lit","lit","lit","lit","lit"};
    // The worn item's colour for each component (items.txt Transform file 1..8 and colours.txt colour; trans 0 none).
    std::array<uint8_t,16> trans{},colors{};
};
Animation loadCharacter(const Archives& archives,const Palette& palette,const std::string& mode,const CharacterAppearance& appearance,const std::string& root="chars");
struct MapPreview { Image image; uint32_t placed{}, missing{}, dependencies{}; };
struct TilePlacement { uint32_t tile{}; int x{},y{}; uint32_t cellX{},cellY{}; bool shadow{}; };
// A scene's tiles, used like a vector of Tile. Each tile is held by a shared pointer, so scenes of the same act kept in
// memory together (the current area and the recently left ones) can hold one copy of a tile they all draw.
class SceneTiles {
public:
    class iterator {
        std::vector<std::shared_ptr<Tile>>::const_iterator at_;
    public:
        explicit iterator(std::vector<std::shared_ptr<Tile>>::const_iterator at):at_(at) {}
        Tile& operator*() const {return **at_;}
        Tile* operator->() const {return at_->get();}
        iterator& operator++() {++at_;return *this;}
        bool operator!=(const iterator& other) const {return at_!=other.at_;}
        bool operator==(const iterator& other) const {return at_==other.at_;}
    };
    SceneTiles()=default;
    SceneTiles(std::initializer_list<Tile> tiles) {for(const auto& tile:tiles)push_back(tile);}
    Tile& operator[](size_t i) {return *items_[i];}
    const Tile& operator[](size_t i) const {return *items_[i];}
    Tile& at(size_t i) {return *items_.at(i);}
    const Tile& at(size_t i) const {return *items_.at(i);}
    size_t size() const {return items_.size();}
    bool empty() const {return items_.empty();}
    void push_back(Tile tile) {items_.push_back(std::make_shared<Tile>(std::move(tile)));}
    void push_back(std::shared_ptr<Tile> tile) {items_.push_back(std::move(tile));}
    void resize(size_t n) {while(items_.size()<n)push_back(Tile{});items_.resize(n);}
    void clear() {items_.clear();}
    const std::shared_ptr<Tile>& shared(size_t i) const {return items_[i];}
    iterator begin() const {return iterator(items_.begin());}
    iterator end() const {return iterator(items_.end());}
private:
    std::vector<std::shared_ptr<Tile>> items_;
};
struct MapScene {
    SceneTiles tiles;
    std::vector<TilePlacement> placements;
    // Animated tiles (DT1 header byte 7: water, lava...): per tile, the tiles it cycles through (its frames, by rarity), empty
    // for a still tile. The game shows the next frame every few ticks.
    std::vector<std::vector<uint32_t>> tileFrames;
    uint32_t width{},height{},missing{},dependencies{};
    uint32_t cellsX{},cellsY{};
    int originX{},originY{};
};
MapScene buildMapScene(const MapLayout& map,const Archives& archives);
Image composeMap(const MapScene& scene);
std::vector<uint32_t> visibleTiles(const MapScene& scene,float left,float top,float right,float bottom);
MapPreview renderMap(const MapLayout& map,const Archives& archives);
}
