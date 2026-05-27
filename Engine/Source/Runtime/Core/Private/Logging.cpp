#include <Core/Assert.h>
#include <Core/Logging.h>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <mutex>
#include <vector>

namespace pe::log {

namespace {

std::mutex                              g_init_mutex;
bool                                    g_initialized = false;
std::vector<spdlog::sink_ptr>           g_sinks;

spdlog::sink_ptr MakeStdoutSink() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    return sink;
}

spdlog::sink_ptr MakeFileSink(const char* log_file_path) {
    // 5 MiB rotating file, keep 3 historical files.
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file_path, 5 * 1024 * 1024, 3);
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    return sink;
}

}  // namespace

// Contract: pe::log::Init must run before any LogCategory is constructed.
// Static-local DECLARE_LOG_CATEGORY instances are constructed on first use, so
// any code path that touches a category must be reached only after Init.
LogCategory::LogCategory(const char* name) : name_(name) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    ENGINE_VERIFY(g_initialized);
    logger_ = std::make_shared<spdlog::logger>(name, g_sinks.begin(), g_sinks.end());
    spdlog::register_logger(logger_);
    logger_->set_level(spdlog::level::trace);
}

void Init(const char* log_file_path) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized) {
        return;
    }

    // Ensure the directory for the log file exists.
    if (log_file_path != nullptr && *log_file_path != '\0') {
        std::filesystem::path path(log_file_path);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    g_sinks.clear();
    g_sinks.push_back(MakeStdoutSink());
    if (log_file_path != nullptr && *log_file_path != '\0') {
        g_sinks.push_back(MakeFileSink(log_file_path));
    }

    // Replace the default logger with one that fans out to all configured sinks.
    auto default_logger = std::make_shared<spdlog::logger>("default", g_sinks.begin(), g_sinks.end());
    default_logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(default_logger);

    g_initialized = true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    spdlog::shutdown();
    g_sinks.clear();
    g_initialized = false;
}

}  // namespace pe::log
