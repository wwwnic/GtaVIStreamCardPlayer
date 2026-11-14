#include <windows.h>

#include "platform/log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace gp {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
}  // namespace

void LogOpen(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (g_file) fclose(g_file);
    g_file = nullptr;
    _wfopen_s(&g_file, path.c_str(), L"w");
}

void LogClose() {
    std::lock_guard lock(g_mutex);
    if (g_file) fclose(g_file);
    g_file = nullptr;
}

void Log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);

    SYSTEMTIME now;
    GetLocalTime(&now);
    std::lock_guard lock(g_mutex);
    if (!g_file) return;
    fprintf(g_file, "%02d:%02d:%02d.%03d %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    fflush(g_file);
}

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

}  // namespace gp
