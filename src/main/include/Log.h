#pragma once
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace bedrock {

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
inline void logf(const char* level, const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[16];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
    std::fprintf(stderr, "[%s %s] %s\n", ts, level, msg);
}

} // namespace bedrock

#define LOG_INFO(...)  ::bedrock::logf("INFO", __VA_ARGS__)
#define LOG_WARN(...)  ::bedrock::logf("WARN", __VA_ARGS__)
#define LOG_ERROR(...) ::bedrock::logf("ERROR", __VA_ARGS__)
