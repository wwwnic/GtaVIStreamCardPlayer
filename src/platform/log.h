// Simple debug log: one text file, rewritten each run, safe from any thread.
#pragma once

#include <string>
#include <string_view>

namespace gp {

// Starts logging to this file (replacing its contents). Until this is called,
// and if the file cannot be opened, Log does nothing.
void LogOpen(const std::wstring& path);
void LogClose();

// printf-style; adds a wall-clock timestamp and a newline.
void Log(const char* format, ...);

std::string ToUtf8(std::wstring_view text);

}  // namespace gp
