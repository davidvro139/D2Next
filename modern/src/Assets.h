#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace d2 {
using Bytes = std::vector<uint8_t>;
using Palette = std::array<std::array<uint8_t, 4>, 256>;
struct Image { uint32_t width{}, height{}; Bytes rgba; };
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
Animation decodeDcc(std::span<const uint8_t> bytes,const Palette& palette);
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
    size_t count() const { return handles_.size(); }
    // The game folder the archives were opened from (another thread can open its own copy: StormLib handles are not shared).
    const std::filesystem::path& root() const { return root_; }
private:
    std::vector<void*> handles_;
    std::filesystem::path root_;
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
struct MapScene {
    std::vector<Tile> tiles;
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
