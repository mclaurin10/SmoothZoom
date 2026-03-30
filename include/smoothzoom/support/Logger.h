#pragma once
// =============================================================================
// SmoothZoom — Diagnostic Logger (Header-Only)
// Dual backend: OutputDebugStringW + optional log file.
//
// Gated by SMOOTHZOOM_LOGGING — compiles to ((void)0) when off.
// Stack-allocated wchar_t buffers only (no heap, no std::string).
// Format: [SmoothZoom:Component] LVL: message
//
// Usage:
//   SZ_LOG_INFO("Main", L"Hooks installed successfully");
//   SZ_LOG_WARN("RenderLoop", L"MagBridge failed, zoom=%.2f", zoom);
//   SZ_LOG_DEBUG("Input", L"Modifier VK changed to 0x%X", vk);
//
// File logging:
//   SmoothZoom::initLogFile(L"C:\\path\\to\\smoothzoom.log");
// =============================================================================

#ifdef _WIN32
#include <windows.h>
#else
// Stub for non-Windows builds (unit testing)
#include <cstdio>
inline void OutputDebugStringW(const wchar_t* s) { fputws(s, stderr); }
#endif

#include <cstdarg>
#include <cstdio>

namespace SmoothZoom
{

enum class LogLevel : int
{
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

// Runtime filter — only messages at or above this level are emitted.
// Default: Info (Debug messages suppressed unless explicitly enabled).
inline LogLevel& logLevelFilter()
{
    static LogLevel level = LogLevel::Info;
    return level;
}

inline void setLogLevel(LogLevel level)
{
    logLevelFilter() = level;
}

namespace detail
{

inline const wchar_t* levelTag(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug: return L"DBG";
    case LogLevel::Info:  return L"INF";
    case LogLevel::Warn:  return L"WRN";
    case LogLevel::Error: return L"ERR";
    default:              return L"???";
    }
}

#ifdef _WIN32

// File logging state — lazily initialized, protected by CRITICAL_SECTION.
struct LogFileState
{
    CRITICAL_SECTION cs;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    bool initialized = false;

    void init()
    {
        InitializeCriticalSection(&cs);
        initialized = true;
    }
};

inline LogFileState& logFileState()
{
    static LogFileState state;
    return state;
}

inline void writeToFile(const wchar_t* line)
{
    auto& state = logFileState();
    if (!state.initialized || state.hFile == INVALID_HANDLE_VALUE)
        return;

    // Convert wide string to UTF-8 for the log file
    // F-06: Sized for worst-case UTF-8 expansion (4 bytes per wchar_t × 640 = 2560)
    char utf8[2600];
    int len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
    if (len <= 0)
        return;
    // len includes null terminator; we don't want to write it
    len -= 1;

    EnterCriticalSection(&state.cs);
    DWORD written;
    WriteFile(state.hFile, utf8, static_cast<DWORD>(len), &written, nullptr);
    FlushFileBuffers(state.hFile);
    LeaveCriticalSection(&state.cs);
}

#else

inline void writeToFile(const wchar_t*) {}

#endif // _WIN32

// Core logging function — stack-allocated buffer, no heap.
inline void logMessage(LogLevel level, const wchar_t* component,
                       const wchar_t* fmt, va_list args)
{
    if (static_cast<int>(level) < static_cast<int>(logLevelFilter()))
        return;

    wchar_t body[512];
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, args);

    wchar_t line[640];
    _snwprintf_s(line, _countof(line), _TRUNCATE,
                 L"[SmoothZoom:%s] %s: %s\n", component, levelTag(level), body);

    OutputDebugStringW(line);
    writeToFile(line);
}

} // namespace detail

#ifdef _WIN32

// Initialize file logging. Call once at startup with the desired log path.
// Creates or truncates the file and writes a session header.
inline void initLogFile(const wchar_t* path)
{
    auto& state = detail::logFileState();
    if (!state.initialized)
        state.init();

    EnterCriticalSection(&state.cs);

    // Close any previously opened file
    if (state.hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(state.hFile);
        state.hFile = INVALID_HANDLE_VALUE;
    }

    state.hFile = CreateFileW(
        path, GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (state.hFile != INVALID_HANDLE_VALUE)
    {
        // Write UTF-8 BOM for editors that need it
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        DWORD written;
        WriteFile(state.hFile, bom, sizeof(bom), &written, nullptr);

        // Write session header
        SYSTEMTIME st;
        GetLocalTime(&st);
        DWORD pid = GetCurrentProcessId();

        char header[256];
        int len = _snprintf_s(header, sizeof(header), _TRUNCATE,
            "=== SmoothZoom log started %04d-%02d-%02d %02d:%02d:%02d (PID %lu) ===\n",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            static_cast<unsigned long>(pid));
        if (len > 0)
            WriteFile(state.hFile, header, static_cast<DWORD>(len), &written, nullptr);

        FlushFileBuffers(state.hFile);
    }

    LeaveCriticalSection(&state.cs);
}

#else

inline void initLogFile(const wchar_t*) {}

#endif // _WIN32

} // namespace SmoothZoom

// =============================================================================
// Public macros — compile to nothing when SMOOTHZOOM_LOGGING is not defined.
// =============================================================================

#ifdef SMOOTHZOOM_LOGGING

// Variadic logging using a helper that accepts ... directly
namespace SmoothZoom { namespace detail {
inline void logFmt(LogLevel level, const wchar_t* component,
                   const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logMessage(level, component, fmt, args);
    va_end(args);
}
}} // namespace SmoothZoom::detail

#define SZ_LOG_DEBUG(component, fmt, ...) \
    SmoothZoom::detail::logFmt(SmoothZoom::LogLevel::Debug, L##component, fmt, ##__VA_ARGS__)
#define SZ_LOG_INFO(component, fmt, ...) \
    SmoothZoom::detail::logFmt(SmoothZoom::LogLevel::Info, L##component, fmt, ##__VA_ARGS__)
#define SZ_LOG_WARN(component, fmt, ...) \
    SmoothZoom::detail::logFmt(SmoothZoom::LogLevel::Warn, L##component, fmt, ##__VA_ARGS__)
#define SZ_LOG_ERROR(component, fmt, ...) \
    SmoothZoom::detail::logFmt(SmoothZoom::LogLevel::Error, L##component, fmt, ##__VA_ARGS__)

#else // SMOOTHZOOM_LOGGING not defined

#define SZ_LOG_DEBUG(component, fmt, ...) ((void)0)
#define SZ_LOG_INFO(component, fmt, ...)  ((void)0)
#define SZ_LOG_WARN(component, fmt, ...)  ((void)0)
#define SZ_LOG_ERROR(component, fmt, ...) ((void)0)

#endif // SMOOTHZOOM_LOGGING
