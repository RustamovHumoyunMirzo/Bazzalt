#pragma once
#include <ryml.hpp>
#include <filesystem>
#include <vector>
#include <map>
#include <cstdint>
#include <string>
namespace Bazzalt::Runtime {
// Shared bounded glTF decoding for animation, skinning, picking and model geometry.
class GltfDocument {
public:
 explicit GltfDocument(const std::filesystem::path& path);
 GltfDocument(const std::vector<std::uint8_t>& bytes,const std::filesystem::path& source={});
 ryml::Tree Tree;
 std::vector<float> Accessor(unsigned index,unsigned dimensions);
 unsigned Count(unsigned index)const;
 static unsigned Index(ryml::ConstNodeRef);
 static unsigned Field(ryml::ConstNodeRef,const char*,unsigned fallback=0);
 static std::string Text(ryml::ConstNodeRef);
private:
 std::vector<std::vector<std::uint8_t>> Buffers;
 std::map<unsigned,std::vector<std::uint8_t>> Views;
 const std::vector<std::uint8_t>& View(unsigned index);
 std::vector<std::uint8_t> Elements(unsigned view,std::size_t offset,std::size_t count,std::size_t element,std::size_t stride=0);
};
}
