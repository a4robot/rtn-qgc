#include "QGCApplication.h"
#include "QGCCommandLineParser.h"
#include "LogManager.h"
#include "QGCLoggingCategory.h"
#include "Platform.h"

#ifdef QGC_UNITTEST_BUILD
    #include "UnitTestList.h"
#endif

QGC_LOGGING_CATEGORY_ON(MainLog, "Main")

#ifdef QGC_AS_SHARED_LIBRARY
#include <thread>
#include <vector>
#include <string>

static std::vector<std::string> g_args_storage;
static std::vector<char*> g_argv;
static int g_argc = 0;
static std::thread* g_ghost_thread = nullptr;

int qgc_main(int argc, char *argv[]);

extern "C" {
    __attribute__((visibility("default"))) void start_qgc_ghost() {
        if (g_ghost_thread) return;

        // Dummy args for the headless ghost
        g_args_storage.push_back("QGroundControl");
        g_args_storage.push_back("--headless");
        g_args_storage.push_back("--bridge-port");
        g_args_storage.push_back("8885"); // default port
        g_args_storage.push_back("--mock-link"); // allow mocklink for now

        for (auto& s : g_args_storage) {
            g_argv.push_back(&s[0]);
        }
        g_argc = g_argv.size();

        g_ghost_thread = new std::thread([]() {
            qgc_main(g_argc, g_argv.data());
        });
        g_ghost_thread->detach();
    }
}

int qgc_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    // --- Parse command line arguments ---
    const auto args = QGCCommandLineParser::parse(argc, argv);
    if (const auto exitCode = QGCCommandLineParser::handleParseResult(args)) {
        return *exitCode;
    }

    // --- Platform initialization ---
    if (const auto exitCode = Platform::initialize(argc, argv, args)) {
        return *exitCode;
    }

    QGCApplication app(argc, argv, args);

    LogManager::installHandler();

    Platform::setupPostApp();

    app.init();

    // Apply after installFilter() (called during app.init) so rules aren't overwritten.
    LogManager::applyEnvironmentLogLevel();

    // --- Run application or tests ---
    const auto run = [&]() -> int {
        using QGCCommandLineParser::AppMode;
        switch (QGCCommandLineParser::determineAppMode(args)) {
#ifdef QGC_UNITTEST_BUILD
        case AppMode::ListTests:
        case AppMode::Test:
            return QGCUnitTest::handleTestOptions(args);
#endif
        case AppMode::BootTest:
            if (!app.bootTestPassed()) {
                qCCritical(MainLog) << "Simple boot test failed during GStreamer initialization";
                return 1;
            }
            qCInfo(MainLog) << "Simple boot test completed";
            return 0;
        case AppMode::Headless:
            // app.init() booted core services via _initForHeadlessBoot();
            // no QML engine or root window exists in this mode.
            qCInfo(MainLog) << "Starting headless event loop";
            return app.exec();
        case AppMode::Gui:
            qCInfo(MainLog) << "Starting application event loop";
            return app.exec();
        }
        Q_UNREACHABLE();
    };

    const int exitCode = run();

    // --- Cleanup ---
    app.shutdown();

    qCInfo(MainLog) << "Exiting main";

    // Destroy LogManager while Qt is still fully functional (before static destruction).
    delete LogManager::instance();

    return exitCode;
}
