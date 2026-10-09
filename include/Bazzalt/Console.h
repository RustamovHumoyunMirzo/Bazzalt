#pragma once
#include "Bazzalt/Export.h"
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Bazzalt {
enum class ConsoleLevel : std::uint8_t { Info=1, Warning=2, Error=4 };
struct ConsoleFilter {
    std::uint8_t Levels=7; // Bit mask: Info=1, Warning=2, Error=4.
    std::string Source;   // Empty matches every source; otherwise exact, case-sensitive.
    std::string Contains; // Case-sensitive UTF-8 substring of message text.
};
struct ConsoleMessage {
    std::uint64_t Id=0;
    std::string Text;
    ConsoleLevel Level=ConsoleLevel::Info;
    std::string Source;
    bool ShowIcon=true;
    double Timestamp=0; // Unix seconds.
};
// Editor-only authoring console. No active editor host => no writes, null reads.
// Indices are zero-based in the unfiltered store; use Id for durable references.
class BAZZALT_API Console final {
public:
    static bool IsAvailable() noexcept;
    static void Log(const std::string& text,ConsoleLevel level=ConsoleLevel::Info,const std::string& source="Script",bool showIcon=true) noexcept;
    static void Info(const std::string& text,const std::string& source="Script",bool showIcon=true) noexcept;
    static void Warning(const std::string& text,const std::string& source="Script",bool showIcon=true) noexcept;
    static void Error(const std::string& text,const std::string& source="Script",bool showIcon=true) noexcept;
    static void Clear(const ConsoleFilter& filter={}) noexcept;
    static void ClearAt(std::size_t index) noexcept;
    static void ClearMessage(std::uint64_t id) noexcept;
    static void ClearMessages(const std::vector<std::uint64_t>& ids) noexcept;
    static std::optional<std::size_t> GetCount(const ConsoleFilter& filter={}) noexcept;
    static std::optional<ConsoleMessage> GetMessageAt(std::size_t index) noexcept;
    static std::optional<ConsoleMessage> FindMessage(std::uint64_t id) noexcept;
    static std::optional<std::vector<ConsoleMessage>> GetMessages(const ConsoleFilter& filter={}) noexcept;
};
}
