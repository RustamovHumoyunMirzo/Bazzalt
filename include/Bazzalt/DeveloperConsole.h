#pragma once
#include "Bazzalt/CVar.h"
#include "Bazzalt/Console.h"
namespace Bazzalt {
struct ConsoleCommand {std::string Name,Description,Usage;bool Cheat=false;};
struct ConsoleResult {bool Success=false;std::string Output;};
/// Runtime command interpreter and bounded output store; separate from editor-only Console logging.
class BAZZALT_API DeveloperConsole final {
public:
    using CommandHandle=std::uint64_t;
    using CommandCallback=std::function<std::string(const std::vector<std::string>&)>;
    static void SetEnabled(bool enabled);
    static bool IsEnabled();
    static void SetVisible(bool visible);
    static bool IsVisible();
    static void SetCheatsAllowed(bool allowed);
    static bool AreCheatsAllowed();
    static CommandHandle RegisterCommand(ConsoleCommand command,CommandCallback callback);
    static bool UnregisterCommand(CommandHandle handle);
    static std::vector<ConsoleCommand> GetCommands(const std::string& prefix="");
    static ConsoleResult Execute(const std::string& line);
    static std::vector<std::string> Complete(const std::string& prefix);
    static std::vector<std::string> GetHistory();
    static void ClearHistory();
    static void SetHistoryLimit(std::size_t limit);
    static void SetMessageLimit(std::size_t limit);
    // Logging and snapshot reads are thread safe; command callbacks run only on the engine thread.
    static void Log(const std::string& text,ConsoleLevel level=ConsoleLevel::Info,const std::string& source="Runtime");
    static std::vector<ConsoleMessage> GetMessages(const ConsoleFilter& filter={});
    static void Clear(const ConsoleFilter& filter={});
};
}
