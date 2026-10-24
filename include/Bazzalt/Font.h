#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Math.h"
#include <string>
#include <cstdint>
namespace Bazzalt {
struct FontInfo {
    bool Valid=false;
    std::string Family,Style;
    std::uint32_t FaceCount=0,GlyphCount=0;
    bool Scalable=false,Kerning=false;
};
// Non-owning imported font reference. No FreeType handles escape the runtime.
class BAZZALT_API Font final {
public:
    Font()=default;
    static Font Load(UUID asset,std::uint32_t face=0){Font result;result.m_asset=asset;result.m_face=face;return result;}
    UUID GetAssetUUID() const{return m_asset;}
    std::uint32_t GetFaceIndex() const{return m_face;}
    bool IsValid() const;
    FontInfo GetInfo() const;
    Vec2 MeasureText(const std::string& text,float size=16,float wrapWidth=0) const;
private:
    UUID m_asset{};std::uint32_t m_face=0;
};
}
