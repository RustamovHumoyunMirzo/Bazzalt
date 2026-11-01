#include "Bazzalt/BinaryData.h"
#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace Bazzalt {
namespace {constexpr std::size_t Maximum=64u*1024u*1024u;void Finite(double value){if(!std::isfinite(value))throw std::invalid_argument("Binary floating-point values must be finite");}}
BinaryWriter::BinaryWriter(std::size_t limit):m_limit(limit){if(!limit||limit>Maximum)throw std::invalid_argument("Binary size limit must be 1..64 MiB");}
void BinaryWriter::Reserve(std::size_t count){if(count>m_limit-m_data.size())throw std::length_error("Binary output exceeds its limit");const auto required=m_data.size()+count;if(required>m_data.capacity())m_data.reserve(std::min(m_limit,std::max(required,std::max<std::size_t>(64,m_data.capacity()*2))));}
void BinaryWriter::WriteUInt8(std::uint8_t value){Reserve(1);m_data.push_back(value);}
void BinaryWriter::WriteUInt16(std::uint16_t value){Reserve(2);for(unsigned i=0;i<2;++i)m_data.push_back(std::uint8_t(value>>(i*8)));}
void BinaryWriter::WriteUInt32(std::uint32_t value){Reserve(4);for(unsigned i=0;i<4;++i)m_data.push_back(std::uint8_t(value>>(i*8)));}
void BinaryWriter::WriteUInt64(std::uint64_t value){Reserve(8);for(unsigned i=0;i<8;++i)m_data.push_back(std::uint8_t(value>>(i*8)));}
void BinaryWriter::WriteInt64(std::int64_t value){WriteUInt64(std::bit_cast<std::uint64_t>(value));}
void BinaryWriter::WriteFloat(float value){Finite(value);WriteUInt32(std::bit_cast<std::uint32_t>(value));}
void BinaryWriter::WriteDouble(double value){Finite(value);WriteUInt64(std::bit_cast<std::uint64_t>(value));}
void BinaryWriter::WriteBool(bool value){WriteUInt8(value?1:0);}
void BinaryWriter::WriteString(const std::string& value){if(value.size()>std::numeric_limits<std::uint32_t>::max())throw std::length_error("String too large");Reserve(4+value.size());WriteUInt32(static_cast<std::uint32_t>(value.size()));m_data.insert(m_data.end(),value.begin(),value.end());}
void BinaryWriter::WriteBytes(const ByteBuffer& value){if(value.size()>std::numeric_limits<std::uint32_t>::max())throw std::length_error("Bytes too large");Reserve(4+value.size());WriteUInt32(static_cast<std::uint32_t>(value.size()));m_data.insert(m_data.end(),value.begin(),value.end());}
void BinaryWriter::WriteUUID(UUID value){WriteUInt64(value.GetHigh());WriteUInt64(value.GetLow());}
void BinaryWriter::WriteVec2(Vec2 v){WriteFloat(v.X);WriteFloat(v.Y);}
void BinaryWriter::WriteVec3(Vec3 v){WriteFloat(v.X);WriteFloat(v.Y);WriteFloat(v.Z);}
void BinaryWriter::WriteVec4(Vec4 v){WriteFloat(v.X);WriteFloat(v.Y);WriteFloat(v.Z);WriteFloat(v.W);}
void BinaryWriter::WriteQuaternion(Quaternion v){WriteFloat(v.X);WriteFloat(v.Y);WriteFloat(v.Z);WriteFloat(v.W);}
ByteBuffer BinaryWriter::TakeData(){ByteBuffer result;result.swap(m_data);return result;}
void BinaryWriter::Clear(){m_data.clear();}
BinaryReader::BinaryReader(ByteBuffer data,std::size_t limit):m_data(std::move(data)){if(!limit||limit>Maximum||m_data.size()>limit)throw std::length_error("Binary input exceeds its limit");}
void BinaryReader::Require(std::size_t count)const{if(count>GetRemaining())throw std::out_of_range("Truncated binary input");}
std::uint8_t BinaryReader::ReadUInt8(){Require(1);return m_data[m_position++];}
std::uint16_t BinaryReader::ReadUInt16(){Require(2);std::uint16_t v=0;for(unsigned i=0;i<2;++i)v|=std::uint16_t(m_data[m_position++])<<(8*i);return v;}
std::uint32_t BinaryReader::ReadUInt32(){Require(4);std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(m_data[m_position++])<<(8*i);return v;}
std::uint64_t BinaryReader::ReadUInt64(){Require(8);std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(m_data[m_position++])<<(8*i);return v;}
std::int64_t BinaryReader::ReadInt64(){return std::bit_cast<std::int64_t>(ReadUInt64());}
float BinaryReader::ReadFloat(){auto v=std::bit_cast<float>(ReadUInt32());Finite(v);return v;}
double BinaryReader::ReadDouble(){auto v=std::bit_cast<double>(ReadUInt64());Finite(v);return v;}
bool BinaryReader::ReadBool(){auto v=ReadUInt8();if(v>1)throw std::invalid_argument("Invalid binary boolean");return v!=0;}
std::string BinaryReader::ReadString(std::size_t max){auto count=ReadUInt32();if(count>max)throw std::length_error("String length exceeds its limit");Require(count);std::string out(m_data.begin()+m_position,m_data.begin()+m_position+count);m_position+=count;return out;}
ByteBuffer BinaryReader::ReadBytes(std::size_t max){auto count=ReadUInt32();if(count>max)throw std::length_error("Byte length exceeds its limit");Require(count);ByteBuffer out(m_data.begin()+m_position,m_data.begin()+m_position+count);m_position+=count;return out;}
UUID BinaryReader::ReadUUID(){auto high=ReadUInt64(),low=ReadUInt64();return UUID{high,low};}
Vec2 BinaryReader::ReadVec2(){auto x=ReadFloat(),y=ReadFloat();return {x,y};}
Vec3 BinaryReader::ReadVec3(){auto x=ReadFloat(),y=ReadFloat(),z=ReadFloat();return {x,y,z};}
Vec4 BinaryReader::ReadVec4(){auto x=ReadFloat(),y=ReadFloat(),z=ReadFloat(),w=ReadFloat();return {x,y,z,w};}
Quaternion BinaryReader::ReadQuaternion(){auto x=ReadFloat(),y=ReadFloat(),z=ReadFloat(),w=ReadFloat();return {x,y,z,w};}
}
