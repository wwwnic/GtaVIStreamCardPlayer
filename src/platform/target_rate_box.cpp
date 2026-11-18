#include "platform/target_rate_box.h"

#include <string>
#include <vector>

#include "core/target_rate.h"

namespace gp {

namespace {

constexpr WORD kNumberId = 100;
constexpr WORD kHintId = 101;
constexpr WORD kLabelId = 102;
constexpr int kMostTyped = 8;  // room for "2000" and some junk; longer is never valid

// A DLGTEMPLATE with its items, built word by word (each item DWORD-aligned).
class Template {
public:
    Template(const wchar_t* title, short width, short height) {
        Align();
        Dword(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        Dword(0);           // extended style
        countAt_ = words_.size();
        Word(0);            // item count, filled in by Add
        Word(0); Word(0);   // x, y: DS_CENTER places it
        Word(static_cast<WORD>(width)); Word(static_cast<WORD>(height));
        Word(0); Word(0);   // no menu, the standard class
        Text(title);
        Word(9);            // point size
        Text(L"Segoe UI");
    }

    // `classAtom`: 0x0080 button, 0x0081 edit, 0x0082 static.
    void Add(WORD id, WORD classAtom, DWORD style, short x, short y, short w, short h, const wchar_t* text) {
        Align();
        Dword(style | WS_CHILD | WS_VISIBLE);
        Dword(0);
        Word(static_cast<WORD>(x)); Word(static_cast<WORD>(y)); Word(static_cast<WORD>(w)); Word(static_cast<WORD>(h));
        Word(id);
        Word(0xFFFF); Word(classAtom);
        Text(text);
        Word(0);  // no creation data
        words_[countAt_]++;
    }

    const DLGTEMPLATE* Get() const { return reinterpret_cast<const DLGTEMPLATE*>(words_.data()); }

private:
    void Word(WORD w) { words_.push_back(w); }
    void Dword(DWORD d) { Word(LOWORD(d)); Word(HIWORD(d)); }
    void Text(const wchar_t* s) {
        for (; *s; s++) Word(static_cast<WORD>(*s));
        Word(0);
    }
    void Align() { if (words_.size() % 2) Word(0); }  // DWORD boundary (the vector's start is aligned)

    std::vector<WORD> words_;
    size_t countAt_ = 0;
};

struct Box {
    int start = 0;
    std::optional<int> result;  // set by OK only: a WM_QUIT (--run, shutdown) ends the box without one
};

std::optional<int> Typed(HWND dialog) {
    wchar_t text[kMostTyped + 2] = {};
    GetDlgItemTextW(dialog, kNumberId, text, static_cast<int>(std::size(text)));
    return TargetRateTyped(text);
}

// OK only for a valid number; the hint only for anything else.
void Check(HWND dialog) {
    const bool valid = Typed(dialog).has_value();
    EnableWindow(GetDlgItem(dialog, IDOK), valid);
    ShowWindow(GetDlgItem(dialog, kHintId), valid ? SW_HIDE : SW_SHOW);
}

INT_PTR CALLBACK BoxProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        auto* box = reinterpret_cast<Box*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const HWND number = GetDlgItem(dialog, kNumberId);
        SendMessageW(number, EM_LIMITTEXT, kMostTyped, 0);
        SetWindowTextW(number, std::to_wstring(box->start).c_str());
        Check(dialog);
        SendMessageW(number, EM_SETSEL, 0, -1);  // typing replaces it
        SetFocus(number);
        return FALSE;  // focus set here
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kNumberId && HIWORD(wParam) == EN_CHANGE) {
            Check(dialog);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK) {  // also Enter
            const std::optional<int> rate = Typed(dialog);
            if (!rate) return TRUE;  // Enter while OK is disabled: stays open
            reinterpret_cast<Box*>(GetWindowLongPtrW(dialog, DWLP_USER))->result = *rate;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {  // also Esc and the close box
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

}  // namespace

std::optional<int> AskTargetRate(HWND owner, int start) {
    // Dialog units: about 4 per character across, 8 per line down.
    Template t(L"Target rate", 164, 62);
    t.Add(kLabelId, 0x0082, SS_LEFT, 7, 9, 72, 8, L"Frames per second:");
    t.Add(kNumberId, 0x0081, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 82, 7, 40, 12, L"");
    t.Add(kHintId, 0x0082, SS_LEFT, 82, 22, 75, 8, kTargetRateHint);
    t.Add(IDOK, 0x0080, BS_DEFPUSHBUTTON | WS_TABSTOP, 53, 40, 50, 14, L"OK");
    t.Add(IDCANCEL, 0x0080, BS_PUSHBUTTON | WS_TABSTOP, 107, 40, 50, 14, L"Cancel");
    Box box;
    box.start = start;
    const INT_PTR answer = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), t.Get(), owner, BoxProc,
                                                   reinterpret_cast<LPARAM>(&box));
    if (answer != IDOK) return std::nullopt;
    return box.result;  // empty unless OK was pressed
}

}  // namespace gp
