#pragma once
#include "Bazzalt/Font.h"
#include <filesystem>
#include <memory>
#include <vector>
namespace Bazzalt::Runtime {
struct FontGlyph {
    std::uint32_t Index=0,Width=0,Height=0;
    float Left=0,Top=0,Advance=0;
    std::vector<std::uint8_t> Alpha;
};
struct FontPreviewImage {std::uint32_t Width=0,Height=0;std::vector<std::uint8_t> Pixels;};
struct ShapedGlyph {std::uint32_t Index=0,Cluster=0;float X=0,Y=0,Advance=0;std::size_t Line=0;};
struct ShapedText {std::vector<ShapedGlyph> Glyphs;Vec2 Size{};std::vector<float> LineWidths;};
class FontFace final {
public:
    FontFace();~FontFace();
    FontFace(const FontFace&)=delete;FontFace& operator=(const FontFace&)=delete;
    bool Load(const std::filesystem::path& path,std::uint32_t face,std::string& error);
    bool SetSize(std::uint32_t pixels);
    FontInfo GetInfo() const;
    FontGlyph Render(std::uint32_t codepoint);
    FontGlyph RenderGlyph(std::uint32_t glyphIndex);
    ShapedText Layout(const std::string& text,float wrapWidth=0,float letterSpacing=0,float lineSpacing=1,bool kerning=true);
    float GetKerning(std::uint32_t left,std::uint32_t right) const;
    float GetAscender() const;float GetLineHeight() const;
private:
    struct Impl;std::unique_ptr<Impl> m_impl;
};
std::uint32_t NextCodepoint(const std::string& text,std::size_t& offset);
bool ValidateGuiAsset(const std::filesystem::path& path,const std::string& kind,std::string& error);
FontInfo InspectFontAsset(UUID asset,std::uint32_t face);
Vec2 MeasureFontAsset(UUID asset,std::uint32_t face,const std::string& text,float size,float wrapWidth);
std::vector<UUID> StylesheetReferences(const std::filesystem::path& path);
FontPreviewImage PreviewFontAsset(UUID asset,std::uint32_t face,const std::string& text,float size,std::uint32_t width);
}
