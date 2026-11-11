#include "runtime.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <utility>

#include "../common/log.h"
#include "../common/progress.h"

namespace
{
std::mutex warning_mutex;
std::vector<std::string> warnings;
} // namespace

namespace hltools::collision::detail
{
std::vector<std::string> take_warnings()
{
    std::lock_guard<std::mutex> lock(warning_mutex);
    return std::exchange(warnings, {});
}
} // namespace hltools::collision::detail

namespace logging
{
void info(const char *, ...) {}
void file(const char *, ...) {}
void console(const char *, ...) {}
void add_warning_summary(const char *message)
{
    std::lock_guard<std::mutex> lock(warning_mutex);
    if (warnings.size() < 128)
        warnings.emplace_back(message);
}
void warn(const char *fmt, ...)
{
    char message[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    add_warning_summary(message);
}
void set_leaked() {}
} // namespace logging

namespace progress
{
void section(const char *) {}
void begin(const char *, int) {}
void set(int) {}
void add(int) {}
void end() {}
void summary(const char *, const char *, double) {}
} // namespace progress
