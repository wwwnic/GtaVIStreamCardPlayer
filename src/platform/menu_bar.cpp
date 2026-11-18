#include "platform/menu_bar.h"

#include "core/menu_model.h"
#include "platform/mf_source.h"

namespace gp {

namespace {

UINT Flags(const MenuItem& item) {
    return MF_STRING | (item.checked ? MF_CHECKED : MF_UNCHECKED) | (item.enabled ? MF_ENABLED : MF_GRAYED);
}

void SetRadio(HMENU menu, UINT id) {
    MENUITEMINFOW info = {sizeof info};
    info.fMask = MIIM_FTYPE;
    info.fType = MFT_STRING | MFT_RADIOCHECK;
    SetMenuItemInfoW(menu, id, FALSE, &info);
}

void Append(HMENU menu, UINT id, const MenuItem& item, bool radio) {
    AppendMenuW(menu, Flags(item), id, item.label.c_str());
    if (radio) SetRadio(menu, id);
}

// Label, check mark and greying of an existing item.
void Update(HMENU menu, UINT id, const MenuItem& item) {
    MENUITEMINFOW info = {sizeof info};
    info.fMask = MIIM_STRING | MIIM_STATE;
    info.dwTypeData = const_cast<wchar_t*>(item.label.c_str());
    info.fState = (item.checked ? MFS_CHECKED : MFS_UNCHECKED) | (item.enabled ? MFS_ENABLED : MFS_GRAYED);
    SetMenuItemInfoW(menu, id, FALSE, &info);
}

// View > Target rate: Monitor and the presets from kMenuTargetMonitor, Custom last.
UINT TargetRateId(size_t index, size_t count) {
    return index + 1 == count ? kMenuTargetCustom : kMenuTargetMonitor + static_cast<UINT>(index);
}

void Clear(HMENU menu) {
    while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
}

void AddPopup(HMENU parent, HMENU popup, const wchar_t* label) {
    AppendMenuW(parent, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(popup), label);
}

}  // namespace

MenuBar::MenuBar() {
    bar_ = CreateMenu();
    device_ = CreatePopupMenu();
    captureCard_ = CreatePopupMenu();
    resolution_ = CreatePopupMenu();
    rate_ = CreatePopupMenu();
    audio_ = CreatePopupMenu();
    view_ = CreatePopupMenu();
    help_ = CreatePopupMenu();

    // The lists are filled as they open; a placeholder keeps them openable.
    AppendMenuW(captureCard_, MF_STRING | MF_GRAYED, kMenuCaptureCardFirst, L"…");
    Append(resolution_, kMenuResolutionDefault, {L"Default (1080p)", true, true}, true);
    AddPopup(device_, captureCard_, L"&Capture Card");
    AddPopup(device_, resolution_, L"&Resolution");
    const std::vector<MenuItem> rates = FrameRateItems(GameRateOverride::Auto);
    for (UINT i = 0; i < rates.size(); i++) Append(rate_, kMenuRateAuto + i, rates[i], true);
    AddPopup(device_, rate_, L"&Frame rate");

    const AudioMenu audio = AudioItems(AudioSettings{});
    Append(audio_, kMenuVolumeUp, {L"Volume up\tUp"}, false);
    Append(audio_, kMenuVolumeDown, {L"Volume down\tDown"}, false);
    Append(audio_, kMenuMute, audio.mute, false);
    AppendMenuW(audio_, MF_SEPARATOR, 0, nullptr);
    Append(audio_, kMenuVolumeShown, audio.volume, false);  // a value, not a command
    AppendMenuW(audio_, MF_SEPARATOR, 0, nullptr);
    Append(audio_, kMenuDelayShown, audio.audioDelay, false);
    Append(audio_, kMenuDelayLater, {L"Audio Delay +50 ms (sound later)\tK"}, false);
    Append(audio_, kMenuDelayEarlier, {L"Audio Delay -50 ms (sound earlier)\tJ"}, false);
    Append(audio_, kMenuDelayReset, audio.resetDelay, false);

    const ViewMenu view = ViewItems(ViewerSettings{}, false);
    Append(view_, kMenuFullscreen, view.fullscreen, false);
    Append(view_, kMenuAlwaysOnTop, view.alwaysOnTop, false);
    Append(view_, kMenuGeneration, view.generation, false);
    targetRate_ = CreatePopupMenu();
    const std::vector<MenuItem> targets = TargetRateItems(kTargetRateMonitor, 60);
    for (UINT i = 0; i < targets.size(); i++) Append(targetRate_, TargetRateId(i, targets.size()), targets[i], true);
    AddPopup(view_, targetRate_, L"&Target rate");
    Append(view_, kMenuSharpUpscale, view.sharpUpscale, false);
    Append(view_, kMenuVsync, view.vsync, false);
    Append(view_, kMenuFpsCounter, view.fpsCounter, false);

    Append(help_, kMenuHotkeys, {L"&Hotkeys"}, false);
    Append(help_, kMenuAbout, {L"&About GamePlayer"}, false);

    AddPopup(bar_, device_, L"&Device");
    AddPopup(bar_, audio_, L"&Audio");
    AddPopup(bar_, view_, L"&View");
    AddPopup(bar_, help_, L"&Help");
}

MenuBar::~MenuBar() {
    // A window destroys its own menu; a detached one (closed in fullscreen) is ours.
    if (bar_ && IsMenu(bar_)) DestroyMenu(bar_);
}

void MenuBar::Opening(HMENU popup, const State& state) {
    if (popup == captureCard_) {
        cards_ = ListCaptureDevices();
        if (cards_.size() > kMenuListMax) cards_.resize(kMenuListMax);
        Clear(captureCard_);
        const std::vector<MenuItem> items = CaptureCardItems(cards_, state.deviceInUse);
        for (UINT i = 0; i < items.size(); i++) Append(captureCard_, kMenuCaptureCardFirst + i, items[i], true);
    }
    if (popup == resolution_) {
        offered_ = OfferedResolutions(state.modes);
        if (offered_.size() > kMenuListMax) offered_.resize(kMenuListMax);
        Clear(resolution_);
        const std::vector<MenuItem> items = ResolutionItems(offered_, state.settings.resolution);
        for (UINT i = 0; i < items.size(); i++) Append(resolution_, kMenuResolutionDefault + i, items[i], true);
    }
    const std::vector<MenuItem> rates = FrameRateItems(state.settings.frameRate);
    for (UINT i = 0; i < rates.size(); i++) Update(rate_, kMenuRateAuto + i, rates[i]);

    const AudioMenu audio = AudioItems(state.settings.audio);
    Update(audio_, kMenuMute, audio.mute);
    Update(audio_, kMenuDelayReset, audio.resetDelay);
    Update(audio_, kMenuVolumeShown, audio.volume);
    Update(audio_, kMenuDelayShown, audio.audioDelay);

    const ViewMenu view = ViewItems(state.settings, state.fullscreen, state.generationAvailability);
    Update(view_, kMenuFullscreen, view.fullscreen);
    Update(view_, kMenuAlwaysOnTop, view.alwaysOnTop);
    Update(view_, kMenuGeneration, view.generation);
    Update(view_, kMenuSharpUpscale, view.sharpUpscale);
    Update(view_, kMenuVsync, view.vsync);
    Update(view_, kMenuFpsCounter, view.fpsCounter);
    const std::vector<MenuItem> targets = TargetRateItems(state.settings.targetRate, state.monitorRefresh);
    for (size_t i = 0; i < targets.size(); i++) Update(targetRate_, TargetRateId(i, targets.size()), targets[i]);
}

std::optional<int> MenuBar::TargetRateFor(UINT id) {
    if (id == kMenuTargetMonitor) return kTargetRateMonitor;
    if (id > kMenuTargetMonitor && id <= kMenuTargetMonitor + kTargetRatePresets.size())
        return kTargetRatePresets[id - kMenuTargetMonitor - 1];
    return std::nullopt;
}

std::optional<CaptureCardEntry> MenuBar::CaptureCardFor(UINT id) const {
    if (id < kMenuCaptureCardFirst || id >= kMenuCaptureCardFirst + cards_.size()) return std::nullopt;
    return cards_[id - kMenuCaptureCardFirst];
}

std::optional<std::optional<Resolution>> MenuBar::ResolutionFor(UINT id) const {
    if (id == kMenuResolutionDefault) return std::optional<Resolution>{};
    if (id < kMenuResolutionFirst || id >= kMenuResolutionFirst + offered_.size()) return std::nullopt;
    return std::optional<Resolution>{offered_[id - kMenuResolutionFirst].resolution};
}

}  // namespace gp
