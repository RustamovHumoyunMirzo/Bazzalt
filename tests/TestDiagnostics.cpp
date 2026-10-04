#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace {
struct TestDiagnostics {
    TestDiagnostics() {
        // A crash must not swallow buffered progress or open a dialog on a CI
        // worker. Assertions and exceptions still fail the test normally.
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
        std::set_terminate([] {
            if (const auto exception = std::current_exception()) {
                try { std::rethrow_exception(exception); }
                catch (const std::exception& error) {
                    std::fprintf(stderr, "Unhandled test exception: %s\n", error.what());
                }
                catch (...) { std::fputs("Unhandled non-standard test exception\n", stderr); }
            } else {
                std::fputs("Test called std::terminate without an active exception\n", stderr);
            }
            std::abort();
        });
    }
} Diagnostics;
}
