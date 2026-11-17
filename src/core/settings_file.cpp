#include "core/settings_file.h"

#include <charconv>
#include <cstdint>
#include <map>
#include <vector>

namespace gp {

namespace {

// UTF-8 here, UTF-16 in the Viewer. Text that is not valid UTF-8 gives nothing.
std::string Utf8Of(const std::wstring& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        uint32_t c = static_cast<uint16_t>(text[i]);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size()) {
            const uint32_t low = static_cast<uint16_t>(text[i + 1]);
            if (low >= 0xDC00 && low < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::optional<std::wstring> WideOf(std::string_view text) {
    std::wstring out;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        int extra;
        uint32_t c;
        if (lead < 0x80) { extra = 0; c = lead; }
        else if ((lead & 0xE0) == 0xC0) { extra = 1; c = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { extra = 2; c = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { extra = 3; c = lead & 0x07; }
        else return std::nullopt;
        if (i + extra >= text.size()) return std::nullopt;  // cut short
        for (int k = 1; k <= extra; k++) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) return std::nullopt;
            c = (c << 6) | (next & 0x3F);
        }
        static constexpr uint32_t kShortestFrom[] = {0, 0x80, 0x800, 0x10000};  // shorter forms are overlong
        if (c < kShortestFrom[extra] || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) return std::nullopt;
        if (c >= 0x10000) {
            c -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (c >> 10));
            out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
        } else {
            out += static_cast<wchar_t>(c);
        }
        i += extra + 1;
    }
    return out;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::optional<int> IntOf(std::string_view s) {
    int value = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc() || end != s.data() + s.size() || s.empty()) return std::nullopt;
    return value;
}

std::optional<bool> BoolOf(std::string_view s) {
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return std::nullopt;
}

std::vector<std::string_view> Split(std::string_view s, char separator) {
    std::vector<std::string_view> parts;
    for (;;) {
        const size_t at = s.find(separator);
        parts.push_back(Trim(s.substr(0, at)));
        if (at == std::string_view::npos) return parts;
        s.remove_prefix(at + 1);
    }
}

const int kMaxCoordinate = 100000;  // far beyond any desktop
const uint32_t kMaxCaptureSide = 8192;

std::optional<WindowRect> RectOf(std::string_view s) {
    const std::vector<std::string_view> parts = Split(s, ',');
    if (parts.size() != 4) return std::nullopt;
    int v[4];
    for (int i = 0; i < 4; i++) {
        const std::optional<int> n = IntOf(parts[i]);
        if (!n || *n < -kMaxCoordinate || *n > kMaxCoordinate) return std::nullopt;
        v[i] = *n;
    }
    const WindowRect r{v[0], v[1], v[2], v[3]};
    if (r.right <= r.left || r.bottom <= r.top) return std::nullopt;
    return r;
}

std::optional<Resolution> ResolutionOf(std::string_view s) {
    const size_t x = s.find('x');
    if (x == std::string_view::npos) return std::nullopt;
    const std::optional<int> w = IntOf(s.substr(0, x)), h = IntOf(s.substr(x + 1));
    if (!w || !h || *w <= 0 || *h <= 0) return std::nullopt;
    if (static_cast<uint32_t>(*w) > kMaxCaptureSide || static_cast<uint32_t>(*h) > kMaxCaptureSide) return std::nullopt;
    return Resolution{static_cast<uint32_t>(*w), static_cast<uint32_t>(*h)};
}

const char* Word(bool b) { return b ? "true" : "false"; }

const char* RateWord(GameRateOverride rate) {
    switch (rate) {
    case GameRateOverride::Fps30: return "30";
    case GameRateOverride::Fps60: return "60";
    default: return "auto";
    }
}

}  // namespace

std::string SettingsToText(const RememberedSettings& settings) {
    const ViewerSettings& v = settings.viewer;
    std::string t;
    t += "# GamePlayer remembers these between runs. Delete this file for the defaults.\n";
    t += "version=" + std::to_string(kSettingsVersion) + "\n";
    if (settings.window) {
        const WindowRect& r = settings.window->normal;
        t += "window=" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," +
             std::to_string(r.bottom) + "\n";
        t += std::string("maximised=") + Word(settings.window->maximised) + "\n";
    }
    t += "volume=" + std::to_string(v.audio.volumePercent) + "\n";
    t += std::string("muted=") + Word(v.audio.muted) + "\n";
    t += "audio_delay_ms=" + std::to_string(v.audio.audioDelayMs) + "\n";
    t += "capture_card=" + Utf8Of(v.captureCard) + "\n";
    t += "capture_card_id=" + Utf8Of(v.captureCardId) + "\n";
    t += "resolution=" +
         (v.resolution ? std::to_string(v.resolution->width) + "x" + std::to_string(v.resolution->height)
                       : std::string("default")) +
         "\n";
    t += std::string("frame_rate=") + RateWord(v.frameRate) + "\n";
    t += std::string("frameGeneration=") + GenerationName(v.generation) + "\n";
    t += std::string("vsync=") + Word(v.vsync) + "\n";
    t += std::string("always_on_top=") + Word(v.alwaysOnTop) + "\n";
    t += std::string("fps_counter=") + Word(v.fpsCounter) + "\n";
    return t;
}

RememberedSettings SettingsFromText(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // a BOM from a hand edit
    std::map<std::string, std::string, std::less<>> values;  // the last of a repeated key wins
    for (std::string_view line : Split(text, '\n')) {
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        values[std::string(Trim(line.substr(0, equals)))] = std::string(Trim(line.substr(equals + 1)));
    }

    RememberedSettings s;
    const auto find = [&](std::string_view key) -> const std::string* {
        const auto at = values.find(key);
        return at == values.end() ? nullptr : &at->second;
    };
    const std::string* version = find("version");
    if (!version || IntOf(*version) != kSettingsVersion) return s;  // missing or unknown version: all defaults

    ViewerSettings& v = s.viewer;
    if (const std::string* w = find("window")) {
        if (const std::optional<WindowRect> r = RectOf(*w)) {
            s.window = SavedWindow{*r, false};
            if (const std::string* m = find("maximised")) s.window->maximised = BoolOf(*m).value_or(false);
        }
    }
    if (const std::string* x = find("volume")) {
        if (const std::optional<int> n = IntOf(*x)) v.audio.volumePercent = ClampVolume(*n);
    }
    if (const std::string* x = find("muted")) v.audio.muted = BoolOf(*x).value_or(false);
    if (const std::string* x = find("audio_delay_ms")) {
        if (const std::optional<int> n = IntOf(*x)) v.audio.audioDelayMs = ClampAudioDelay(*n);
    }
    if (const std::string* x = find("capture_card")) v.captureCard = WideOf(*x).value_or(L"");
    if (const std::string* x = find("capture_card_id")) v.captureCardId = WideOf(*x).value_or(L"");
    if (v.captureCard.empty()) v.captureCardId.clear();  // automatic: no device to look for
    if (const std::string* x = find("resolution")) v.resolution = ResolutionOf(*x);
    if (const std::string* x = find("frame_rate")) {
        if (*x == "30") v.frameRate = GameRateOverride::Fps30;
        else if (*x == "60") v.frameRate = GameRateOverride::Fps60;
    }
    if (const auto* x = find("frameGeneration")) {
        if (*x == "2x") v.generation = GenerationFactor::Double;
        if (*x == "4x") v.generation = GenerationFactor::Quadruple;
    }
    if (const std::string* x = find("vsync")) v.vsync = BoolOf(*x).value_or(false);
    if (const std::string* x = find("always_on_top")) v.alwaysOnTop = BoolOf(*x).value_or(false);
    if (const std::string* x = find("fps_counter")) v.fpsCounter = BoolOf(*x).value_or(false);
    return s;
}

bool SettingsSaveTiming::Second(const std::string& now) {
    const bool settled = previous_ && *previous_ == now;
    previous_ = now;
    if (!settled || now == onDisk_) return false;
    if (failed_ && *failed_ == now && ++secondsSinceFailure_ < kRetrySeconds) return false;
    return true;
}

}  // namespace gp
