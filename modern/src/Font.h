#pragma once
#include "Assets.h"
#include <map>
#include <memory>
namespace d2 {
// One of the game's bitmap fonts (data\local\font\latin\NAME.dc6 glyphs + NAME.tbl metrics).
class Font {
public:
    Font(const Archives& archives,const Palette& palette,const std::string& name);
    // Width in pixels of one line of text.
    int width(const std::string& text) const;
    // Line height: the height of the tallest glyph.
    int lineHeight() const {return lineHeight_;}
    // The text as an image, lines split at '\n' and centred on each other when `centered`. Glyph colours are multiplied by `color`.
    Image render(const std::string& text,std::array<uint8_t,3> color,bool centered=false) const;
private:
    struct Glyph {uint8_t width=0,height=0;uint16_t frame=0;bool present=false;};
    std::array<Glyph,256> glyphs_{};
    std::vector<Image> frames_;
    int lineHeight_=0;
};
// Rendered strings, kept for the life of the cache so their addresses can key GPU textures.
class TextCache {
public:
    explicit TextCache(const Font& font):font_(font) {}
    const Image& get(const std::string& text,std::array<uint8_t,3> color,bool centered=false);
    const Font& font() const {return font_;}
private:
    const Font& font_;
    std::map<std::string,std::unique_ptr<Image>> images_;
};
}
