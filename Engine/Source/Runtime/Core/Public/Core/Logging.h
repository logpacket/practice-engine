// Logging.h - logging facade built on spdlog.
//
// Usage:
//   // In one .cpp per module:
//   DECLARE_LOG_CATEGORY(LogCore);
//
//   // Anywhere:
//   ENGINE_LOG_INFO(LogCore, "Booted in {} ms", elapsed);
//
// Stage 1 limitation: spdlog is statically linked into each module, so each .so
// has its own spdlog runtime. Stdout writes from all modules interleave correctly
// (the OS owns the fd), but only Core writes to Saved/Logs/engine.log.
// Stage 2 will unify logging via a Core-exposed log forward (Architecture.md §9).

#pragma once

#include <Core/CoreAPI.h>

#include <spdlog/spdlog.h>

#include <memory>

namespace pe::log {

// Per-category logger. One static instance per category per translation unit.
class CORE_API LogCategory {
public:
    explicit LogCategory(const char* name);
    spdlog::logger& Get() noexcept { return *logger_; }
    const char*     Name() const noexcept { return name_; }

private:
    const char*                     name_;
    std::shared_ptr<spdlog::logger> logger_;
};

// One-time initialization. Sets up the default logger with a colored stdout sink
// and a rotating file sink at `log_file_path` (created if missing).
// Idempotent across calls within the same .so but each .so has its own spdlog
// runtime; consequently Core init covers Core only.
CORE_API void Init(const char* log_file_path);

// Flush and drop all loggers in this .so's spdlog runtime.
CORE_API void Shutdown();

}  // namespace pe::log

// Declare a category. Place once per .cpp / .h that needs the category.
// Returns a reference to a static-local LogCategory; thread-safe by C++11 magic statics.
#define DECLARE_LOG_CATEGORY(name)                                  \
    inline ::pe::log::LogCategory& name() {                         \
        static ::pe::log::LogCategory instance{#name};              \
        return instance;                                            \
    }

// Use unqualified `category()` so ADL / ordinary lookup finds the function in
// whatever namespace it was declared (typically the same namespace as the caller).
#define ENGINE_LOG_TRACE(category, ...) category().Get().trace(__VA_ARGS__)
#define ENGINE_LOG_DEBUG(category, ...) category().Get().debug(__VA_ARGS__)
#define ENGINE_LOG_INFO(category, ...)  category().Get().info(__VA_ARGS__)
#define ENGINE_LOG_WARN(category, ...)  category().Get().warn(__VA_ARGS__)
#define ENGINE_LOG_ERROR(category, ...) category().Get().error(__VA_ARGS__)
