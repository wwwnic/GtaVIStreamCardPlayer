// The remembered settings file on disk (ticket 09). Only reading and writing
// bytes; what they mean is core/settings_file.h.
#pragma once

#include <string>

namespace gp {

// %APPDATA%\GamePlayer\settings.ini, from SHGetKnownFolderPath
// (FOLDERID_RoamingAppData). Empty if the folder is not known.
std::wstring DefaultSettingsPath();

// The whole file; "" when it is missing, unreadable or implausibly large.
// A file another program holds open is waited for briefly (up to ~0.5 s).
std::string ReadSettingsFile(const std::wstring& path);

// Writes `text` to a temporary file beside `path`, then renames it over
// `path`, so a half-written file is never left as the settings. Creates the
// folder if needed. False if anything failed (the old file is then intact),
// with `error` the Windows error of the step that failed.
bool WriteSettingsFile(const std::wstring& path, const std::string& text, unsigned long& error);

}  // namespace gp
