#pragma once
#include <cstdint>
#include "Bazzalt/Export.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
struct AudioClipInfo {
    double Duration=0;
    std::uint64_t FrameCount=0;
    std::uint32_t Channels=0, SampleRate=0;
};
// A stable asset reference; the runtime owns decoders and streaming buffers.
class BAZZALT_API AudioClip final {
public:
    AudioClip()=default;
    static AudioClip Load(UUID id) { AudioClip result;result.m_id=id;return result; }
    UUID GetAssetUUID() const { return m_id; }
    bool IsValid() const;
    AudioClipInfo GetInfo() const;
private:
    UUID m_id{};
};
}
