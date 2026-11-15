#include "platform/settings_store.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

namespace gp {

namespace {

const DWORD kMaxSettingsBytes = 64 * 1024;  // the real file is well under 1 KB

std::wstring FolderOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

}  // namespace

std::wstring DefaultSettingsPath() {
    PWSTR folder = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder)) && folder) {
        path = std::wstring(folder) + L"\\GamePlayer\\settings.ini";
    }
    CoTaskMemFree(folder);
    return path;
}

std::string ReadSettingsFile(const std::wstring& path) {
    if (path.empty()) return {};
    HANDLE file = INVALID_HANDLE_VALUE;
    // Held open by another program for a moment (a scanner, an editor saving):
    // wait a little rather than start with defaults that would replace the file.
    for (int attempt = 0; attempt < 10; attempt++) {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) break;
        Sleep(50);
    }
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxSettingsBytes) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
        text.resize(read);
    }
    CloseHandle(file);
    return text;
}

bool WriteSettingsFile(const std::wstring& path, const std::string& text, unsigned long& error) {
    error = ERROR_INVALID_NAME;
    if (path.empty()) return false;
    const std::wstring folder = FolderOf(path);
    if (!folder.empty()) CreateDirectoryW(folder.c_str(), nullptr);  // fails harmlessly if it exists
    const std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size() && FlushFileBuffers(file);
    if (!ok) error = GetLastError();  // before the clean-up calls change it
    CloseHandle(file);
    if (ok && !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        ok = false;
    }
    if (!ok) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

}  // namespace gp
