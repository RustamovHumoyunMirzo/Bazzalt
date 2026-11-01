#include "Bazzalt/SaveGame.h"
#include <stdexcept>
namespace Bazzalt {
namespace {
constexpr std::size_t MaxBytes=16u*1024u*1024u,MaxNodes=100000,MaxDepth=32;
void Key(const std::string& key){if(key.empty()||key.size()>256||key.find('\0')!=std::string::npos)throw std::invalid_argument("Save keys must be 1..256 bytes without NUL");}
void Write(BinaryWriter& writer,const SaveValue& value,std::size_t depth,std::size_t& nodes){
    if(depth>MaxDepth||++nodes>MaxNodes)throw std::length_error("Save document is too complex");
    writer.WriteUInt8(static_cast<std::uint8_t>(value.GetType()));
    std::visit([&](const auto& field){using T=std::decay_t<decltype(field)>;
        if constexpr(std::is_same_v<T,std::monostate>){}
        else if constexpr(std::is_same_v<T,bool>)writer.WriteBool(field);
        else if constexpr(std::is_same_v<T,std::int64_t>)writer.WriteInt64(field);
        else if constexpr(std::is_same_v<T,double>)writer.WriteDouble(field);
        else if constexpr(std::is_same_v<T,std::string>){if(field.size()>1048576)throw std::length_error("Save string exceeds 1 MiB");writer.WriteString(field);}
        else if constexpr(std::is_same_v<T,Vec2>)writer.WriteVec2(field);
        else if constexpr(std::is_same_v<T,Vec3>)writer.WriteVec3(field);
        else if constexpr(std::is_same_v<T,Vec4>)writer.WriteVec4(field);
        else if constexpr(std::is_same_v<T,Quaternion>)writer.WriteQuaternion(field);
        else if constexpr(std::is_same_v<T,UUID>)writer.WriteUUID(field);
        else if constexpr(std::is_same_v<T,ByteBuffer>)writer.WriteBytes(field);
        else if constexpr(std::is_same_v<T,SaveArray>){if(field.size()>MaxNodes-nodes)throw std::length_error("Too many save array items");writer.WriteUInt32(static_cast<std::uint32_t>(field.size()));for(auto& item:field)Write(writer,item,depth+1,nodes);}
        else {if(field.size()>MaxNodes-nodes)throw std::length_error("Too many save object fields");writer.WriteUInt32(static_cast<std::uint32_t>(field.size()));for(auto& [key,item]:field){Key(key);writer.WriteString(key);Write(writer,item,depth+1,nodes);}}
    },value.Data);
}
SaveValue Read(BinaryReader& reader,std::size_t depth,std::size_t& nodes){
    if(depth>MaxDepth||++nodes>MaxNodes)throw std::length_error("Save document is too complex");
    switch(static_cast<SaveValueType>(reader.ReadUInt8())){
    case SaveValueType::Null:return {};
    case SaveValueType::Boolean:return SaveValue(reader.ReadBool());
    case SaveValueType::Integer:return SaveValue(reader.ReadInt64());
    case SaveValueType::Number:return SaveValue(reader.ReadDouble());
    case SaveValueType::String:return SaveValue(reader.ReadString());
    case SaveValueType::Vec2:return SaveValue(reader.ReadVec2());
    case SaveValueType::Vec3:return SaveValue(reader.ReadVec3());
    case SaveValueType::Vec4:return SaveValue(reader.ReadVec4());
    case SaveValueType::Quaternion:return SaveValue(reader.ReadQuaternion());
    case SaveValueType::UUID:return SaveValue(reader.ReadUUID());
    case SaveValueType::Bytes:return SaveValue(reader.ReadBytes(MaxBytes));
    case SaveValueType::Array:{auto count=reader.ReadUInt32();if(count>MaxNodes-nodes||count>reader.GetRemaining())throw std::length_error("Invalid save array count");SaveArray values;values.reserve(count);for(std::uint32_t i=0;i<count;++i)values.push_back(Read(reader,depth+1,nodes));return SaveValue(std::move(values));}
    case SaveValueType::Object:{auto count=reader.ReadUInt32();if(count>MaxNodes-nodes||count>reader.GetRemaining()/6)throw std::length_error("Invalid save field count");SaveObject values;for(std::uint32_t i=0;i<count;++i){auto key=reader.ReadString(256);Key(key);auto value=Read(reader,depth+1,nodes);if(!values.emplace(std::move(key),std::move(value)).second)throw std::invalid_argument("Duplicate save key");}return SaveValue(std::move(values));}
    default:throw std::invalid_argument("Unknown save value type");
    }
}
}
void SaveDocument::Set(const std::string& key,SaveValue value){Key(key);if(!m_fields.contains(key)&&m_fields.size()>=MaxNodes)throw std::length_error("Too many save fields");m_fields.insert_or_assign(key,std::move(value));}
std::optional<SaveValue> SaveDocument::Get(const std::string& key)const{auto found=m_fields.find(key);return found==m_fields.end()?std::nullopt:std::optional<SaveValue>(found->second);}
bool SaveDocument::Has(const std::string& key)const{return m_fields.contains(key);}
bool SaveDocument::Remove(const std::string& key){return m_fields.erase(key)!=0;}
void SaveDocument::Clear(){m_fields.clear();}
bool SaveDocument::Migrate(std::uint32_t target,const std::function<bool(SaveDocument&,std::uint32_t,std::uint32_t)>& migration){if(!target||target<SchemaVersion)throw std::invalid_argument("Cannot downgrade save schema");if(target==SchemaVersion)return true;if(!migration)throw std::invalid_argument("Missing save migration");auto candidate=*this;if(!migration(candidate,SchemaVersion,target))return false;candidate.SchemaVersion=target;(void)SaveGame::Encode(candidate);*this=std::move(candidate);return true;}
ByteBuffer SaveGame::Encode(const SaveDocument& document){if(!document.SchemaVersion)throw std::invalid_argument("Save schema version must be positive");BinaryWriter writer(MaxBytes);writer.WriteUInt32(0x31445A42);writer.WriteUInt32(document.SchemaVersion);std::size_t nodes=0;Write(writer,SaveValue(document.GetFields()),0,nodes);return writer.TakeData();}
SaveDocument SaveGame::Decode(const ByteBuffer& bytes){BinaryReader reader(bytes,MaxBytes);if(reader.ReadUInt32()!=0x31445A42)throw std::invalid_argument("Unsupported save document format");SaveDocument result;result.SchemaVersion=reader.ReadUInt32();if(!result.SchemaVersion)throw std::invalid_argument("Invalid save schema");std::size_t nodes=0;auto value=Read(reader,0,nodes);if(value.GetType()!=SaveValueType::Object||!reader.IsAtEnd())throw std::invalid_argument("Invalid save document root/trailing data");for(auto& [key,field]:std::get<SaveObject>(value.Data))result.Set(key,std::move(field));return result;}
}
