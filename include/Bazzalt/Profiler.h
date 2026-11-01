#pragma once
#include "Bazzalt/Export.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace Bazzalt {
struct ProfileSample {
    std::string Name;
    double StartMicroseconds = 0, DurationMicroseconds = 0;
    std::uint64_t Thread = 0;
};
class BAZZALT_API Profiler final {
public:
    Profiler() = delete;
    /// Starts a new bounded capture. Old in-flight scopes cannot enter it.
    static void BeginCapture(std::size_t capacity = 100000);
    static void EndCapture();
    static bool IsCapturing();
    static std::vector<ProfileSample> GetSamples();
    static std::uint64_t GetDroppedSampleCount();
    /// Chrome/Perfetto trace JSON. File failures throw; caller owns the path.
    static void ExportTrace(const std::filesystem::path& path);
};
class BAZZALT_API ProfileScope final {
public:
    explicit ProfileScope(const char* name);
    ~ProfileScope() noexcept;
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
private:
    std::string m_name;
    std::uint64_t m_generation = 0;
    std::chrono::steady_clock::time_point m_start;
};
}
