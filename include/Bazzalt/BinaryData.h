#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace Bazzalt {
using ByteBuffer = std::vector<std::uint8_t>;
/// Bounded, little-endian binary codec. Independent of compiler padding and host endianness.
class BAZZALT_API BinaryWriter final {
public:
    explicit BinaryWriter(std::size_t limit = 65536);
    void WriteUInt8(std::uint8_t value);
    void WriteUInt16(std::uint16_t value);
    void WriteUInt32(std::uint32_t value);
    void WriteUInt64(std::uint64_t value);
    void WriteInt64(std::int64_t value);
    void WriteFloat(float value);
    void WriteDouble(double value);
    void WriteBool(bool value);
    void WriteString(const std::string& value);
    void WriteBytes(const ByteBuffer& value);
    void WriteUUID(UUID value);
    void WriteVec2(Vec2 value);
    void WriteVec3(Vec3 value);
    void WriteVec4(Vec4 value);
    void WriteQuaternion(Quaternion value);
    const ByteBuffer& GetData() const { return m_data; }
    ByteBuffer TakeData();
    std::size_t GetSize() const { return m_data.size(); }
    void Clear();
private:
    void Reserve(std::size_t bytes);
    ByteBuffer m_data;
    std::size_t m_limit;
};
class BAZZALT_API BinaryReader final {
public:
    explicit BinaryReader(ByteBuffer data, std::size_t limit = 65536);
    std::uint8_t ReadUInt8();
    std::uint16_t ReadUInt16();
    std::uint32_t ReadUInt32();
    std::uint64_t ReadUInt64();
    std::int64_t ReadInt64();
    float ReadFloat();
    double ReadDouble();
    bool ReadBool();
    std::string ReadString(std::size_t maximum = 1048576);
    ByteBuffer ReadBytes(std::size_t maximum = 16777216);
    UUID ReadUUID();
    Vec2 ReadVec2();
    Vec3 ReadVec3();
    Vec4 ReadVec4();
    Quaternion ReadQuaternion();
    std::size_t GetRemaining() const { return m_data.size()-m_position; }
    bool IsAtEnd() const { return m_position==m_data.size(); }
private:
    void Require(std::size_t bytes) const;
    ByteBuffer m_data;
    std::size_t m_position=0;
};
}
