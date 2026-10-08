#pragma once
#include <cstdint>
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {
enum class TextureColorFormat : std::uint8_t { RGBA8, RGBA16F, RGBA32F };
enum class TextureDepthFormat : std::uint8_t { None, Depth16, Depth24, Depth32F };
enum class TextureFilter : std::uint8_t { Point, Linear, Trilinear };
enum class TextureWrap : std::uint8_t { Clamp, Repeat, Mirror };
struct TextureDescriptor {
    std::uint32_t Width=512,Height=512;
    std::uint8_t Samples=1;
    bool Mipmaps=false;
    std::uint8_t MipLevels=0; // Zero: full pyramid when Mipmaps is enabled.
    TextureColorFormat ColorFormat=TextureColorFormat::RGBA8;
    TextureDepthFormat DepthFormat=TextureDepthFormat::Depth32F;
    TextureFilter Filter=TextureFilter::Linear;
    TextureWrap Wrap=TextureWrap::Clamp;
    Vec4 ClearColor{0,0,0,1};
    [[nodiscard]] BAZZALT_API bool IsValid() const;
    [[nodiscard]] BAZZALT_API std::uint8_t GetMipLevelCount() const;
};
// Stable texture asset / runtime-resource handle, independent of its producer.
// Descriptor changes are runtime overrides, never writes to project files.
class BAZZALT_API Texture final {
public:
    Texture()=default;
    [[nodiscard]] static Texture Load(UUID id){Texture value;value.m_id=id;return value;}
    [[nodiscard]] static Texture Create(const TextureDescriptor& descriptor={});
    [[nodiscard]] UUID GetAssetUUID() const{return m_id;}
    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] bool IsRenderTarget() const;
    [[nodiscard]] bool IsRuntime() const;
    [[nodiscard]] TextureDescriptor GetDescriptor() const;
    void SetDescriptor(const TextureDescriptor& descriptor);
    void ResetDescriptor();
    void Resize(std::uint32_t width,std::uint32_t height);
    bool Destroy(); // Runtime textures only; imported assets are never deleted.
private:
    UUID m_id{};
};
}
