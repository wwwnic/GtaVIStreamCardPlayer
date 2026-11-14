// THROWAWAY PROTOTYPE for ticket 01. Nothing here is reused as app code.
// Capture Card Feed -> Media Foundation Source Reader (async, no converters)
// -> own YUV->RGB pixel shader -> D3D11 flip-model swap chain, present on arrival, tearing allowed.
// Sound: WASAPI shared capture -> small ring -> WASAPI shared render (default device).
// Keys: F11 borderless fullscreen | D toggle Repeated Frame dropping | T toggle near-duplicate tolerance | Esc leave fullscreen
// Args: --run N  (exit by itself after N seconds, for unattended checks)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <wrl/client.h>
#include <atomic>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwctype>

using Microsoft::WRL::ComPtr;
static const DWORD kVid = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;

// ---------------------------------------------------------------- log
static FILE* g_logFile = nullptr;
static CRITICAL_SECTION g_logCs;

static void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    EnterCriticalSection(&g_logCs);
    if (g_logFile) {
        fprintf(g_logFile, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fflush(g_logFile);
    }
    LeaveCriticalSection(&g_logCs);
}

static std::string U8(const wchar_t* w) {
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}

static bool ContainsI(const std::wstring& hay, const wchar_t* needle) {
    std::wstring h = hay, n = needle;
    for (auto& c : h) c = (wchar_t)towlower(c);
    for (auto& c : n) c = (wchar_t)towlower(c);
    return h.find(n) != std::wstring::npos;
}

// ---------------------------------------------------------------- shared state
enum { FMT_YUY2 = 0, FMT_NV12 = 1 };
static int g_fmt = FMT_YUY2;
static UINT g_vw = 1920, g_vh = 1080;
static UINT g_fullRange = 0, g_m709 = 1;
static size_t g_frameBytes = 0;

static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_drop{true};
static std::atomic<bool> g_tol{false};

static std::atomic<uint32_t> c_arrived{0}, c_unique{0}, c_repeated{0}, c_near{0}, c_replaced{0}, c_presented{0};

static CRITICAL_SECTION g_frameCs;
static std::vector<uint8_t> g_pending, g_upload, g_last;
static bool g_havePending = false, g_haveLast = false;
static HANDLE g_frameEvent = nullptr;

static void ProcessFrame(const BYTE* p, size_t len) {
    static bool warned = false;
    c_arrived++;
    if (len != g_frameBytes) {
        if (!warned) { Log("WARN buffer length %zu != expected %zu (stride/padding?). Frames skipped.", len, g_frameBytes); warned = true; }
        return;
    }
    size_t words = len / 8;
    size_t diff = 0;
    if (g_haveLast) {
        const uint8_t* l = g_last.data();
        for (size_t i = 0; i < words; i++) {
            uint64_t a, b;
            memcpy(&a, p + i * 8, 8);
            memcpy(&b, l + i * 8, 8);
            diff += (a != b);
        }
    }
    size_t nearLimit = words / 200;  // 0.5% of 8-byte words
    bool tol = g_tol.load();
    bool unique = !g_haveLast || diff > (tol ? nearLimit : 0);
    if (g_haveLast && diff > 0 && diff <= nearLimit) c_near++;
    memcpy(g_last.data(), p, len);
    g_haveLast = true;
    if (unique) c_unique++; else c_repeated++;
    if (unique || !g_drop.load()) {
        EnterCriticalSection(&g_frameCs);
        if (g_havePending) c_replaced++;
        memcpy(g_pending.data(), p, len);
        g_havePending = true;
        LeaveCriticalSection(&g_frameCs);
        SetEvent(g_frameEvent);
    }
}

// ---------------------------------------------------------------- Media Foundation
struct SrCallback : IMFSourceReaderCallback {
    LONG ref = 1;
    ComPtr<IMFSourceReader> reader;
    STDMETHODIMP QueryInterface(REFIID riid, void** o) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFSourceReaderCallback)) { *o = static_cast<IMFSourceReaderCallback*>(this); AddRef(); return S_OK; }
        *o = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&ref); if (!r) delete this; return r; }
    STDMETHODIMP OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG, IMFSample* s) override {
        if (g_quit) return S_OK;
        if (FAILED(hr)) { Log("ReadSample callback error hr=0x%08lX", (unsigned long)hr); return S_OK; }
        if (flags & MF_SOURCE_READERF_ERROR) { Log("Source reader reported stream error flag"); return S_OK; }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) Log("Media type changed mid-stream");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { Log("End of stream"); return S_OK; }
        if (s) {
            ComPtr<IMFMediaBuffer> b;
            if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
                BYTE* p = nullptr; DWORD len = 0;
                if (SUCCEEDED(b->Lock(&p, nullptr, &len))) { ProcessFrame(p, len); b->Unlock(); }
            }
        }
        reader->ReadSample(kVid, 0, nullptr, nullptr, nullptr, nullptr);
        return S_OK;
    }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
};

static ComPtr<IMFMediaSource> g_source;
static SrCallback* g_cb = nullptr;
static std::wstring g_videoName;
static std::string g_status = "no video";

static std::string SubtypeName(const GUID& g) {
    if (g == MFVideoFormat_YUY2) return "YUY2";
    if (g == MFVideoFormat_NV12) return "NV12";
    if (g == MFVideoFormat_RGB24) return "RGB24";
    if (g == MFVideoFormat_RGB32) return "RGB32";
    if (g == MFVideoFormat_MJPG) return "MJPG";
    if (g == MFVideoFormat_P010) return "P010";
    if (g == MFVideoFormat_UYVY) return "UYVY";
    char b[64];
    uint32_t d = g.Data1;
    char c[5] = { (char)(d & 255), (char)((d >> 8) & 255), (char)((d >> 16) & 255), (char)((d >> 24) & 255), 0 };
    snprintf(b, sizeof b, "other(%s)", c);
    return b;
}

static std::string DescribeType(IMFMediaType* t) {
    GUID st = {};
    t->GetGUID(MF_MT_SUBTYPE, &st);
    UINT32 w = 0, h = 0, n = 0, d = 1;
    MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &n, &d);
    UINT32 il = MFGetAttributeUINT32(t, MF_MT_INTERLACE_MODE, 0xFFFF);
    UINT32 rng = MFGetAttributeUINT32(t, MF_MT_VIDEO_NOMINAL_RANGE, 0xFFFF);
    UINT32 mat = MFGetAttributeUINT32(t, MF_MT_YUV_MATRIX, 0xFFFF);
    UINT32 prim = MFGetAttributeUINT32(t, MF_MT_VIDEO_PRIMARIES, 0xFFFF);
    UINT32 xfer = MFGetAttributeUINT32(t, MF_MT_TRANSFER_FUNCTION, 0xFFFF);
    UINT32 stride = MFGetAttributeUINT32(t, MF_MT_DEFAULT_STRIDE, 0);
    UINT32 mn = 0, md = 1, xn = 0, xd = 1;
    bool hasRange = SUCCEEDED(MFGetAttributeRatio(t, MF_MT_FRAME_RATE_RANGE_MIN, &mn, &md)) && mn != 0;
    MFGetAttributeRatio(t, MF_MT_FRAME_RATE_RANGE_MAX, &xn, &xd);
    char b[400];
    snprintf(b, sizeof b, "%-6s %ux%u @ %u/%u = %.3f fps  range[%.2f..%.2f]  interlace=%u nomRange=%u yuvMatrix=%u primaries=%u transfer=%u stride=%d",
             SubtypeName(st).c_str(), w, h, n, d, d ? (double)n / d : 0.0,
             hasRange ? (double)mn / md : 0.0, xd ? (double)xn / xd : 0.0,
             il, rng, mat, prim, xfer, (int)stride);
    return b;
}

static bool StartCapture() {
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 1);
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(attr.Get(), &devs, &count);
    if (FAILED(hr)) { Log("MFEnumDeviceSources failed hr=0x%08lX", (unsigned long)hr); g_status = "enum failed"; return false; }
    Log("Video capture devices: %u", count);
    int pick = -1;
    std::vector<std::wstring> names(count);
    for (UINT32 i = 0; i < count; i++) {
        WCHAR* nm = nullptr; UINT32 l = 0;
        devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nm, &l);
        names[i] = nm ? nm : L"";
        CoTaskMemFree(nm);
        WCHAR* link = nullptr;
        devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &l);
        Log("  [%u] \"%s\"  link=%s", i, U8(names[i].c_str()).c_str(), U8(link).c_str());
        CoTaskMemFree(link);
        if (pick < 0 && (ContainsI(names[i], L"gc553") || ContainsI(names[i], L"live gamer") || ContainsI(names[i], L"avermedia"))) pick = (int)i;
    }
    if (count == 0) { Log("No video capture device found."); g_status = "NO VIDEO DEVICE"; CoTaskMemFree(devs); return false; }
    if (pick < 0) { pick = 0; Log("No name match for GC553 Pro; falling back to first device."); }
    else Log("Matched device by name: [%d]", pick);
    g_videoName = names[pick];

    hr = devs[pick]->ActivateObject(IID_PPV_ARGS(&g_source));
    for (UINT32 i = 0; i < count; i++) devs[i]->Release();
    CoTaskMemFree(devs);
    if (FAILED(hr)) {
        Log("ActivateObject (open device) FAILED hr=0x%08lX  -- card may be held by another program (OBS / AVerMedia software)", (unsigned long)hr);
        g_status = "OPEN FAILED (card busy?)";
        return false;
    }

    g_cb = new SrCallback();
    ComPtr<IMFAttributes> ra;
    MFCreateAttributes(&ra, 4);
    ra->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, g_cb);
    ra->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);   // no hidden converters
    ra->SetUINT32(MF_LOW_LATENCY, TRUE);
    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromMediaSource(g_source.Get(), ra.Get(), &reader);
    if (FAILED(hr)) { Log("MFCreateSourceReaderFromMediaSource failed hr=0x%08lX", (unsigned long)hr); g_status = "reader failed"; return false; }
    g_cb->reader = reader;

    Log("Native media types:");
    struct Cand { DWORD idx; GUID sub; UINT32 w, h, n, d; };
    std::vector<Cand> cands;
    for (DWORD i = 0;; i++) {
        ComPtr<IMFMediaType> t;
        hr = reader->GetNativeMediaType(kVid, i, &t);
        if (hr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) { Log("  GetNativeMediaType(%lu) failed hr=0x%08lX", (unsigned long)i, (unsigned long)hr); break; }
        Log("  #%lu %s", (unsigned long)i, DescribeType(t.Get()).c_str());
        Cand c = {}; c.idx = i; c.d = 1;
        t->GetGUID(MF_MT_SUBTYPE, &c.sub);
        MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &c.w, &c.h);
        MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &c.n, &c.d);
        cands.push_back(c);
    }
    bool has1080p30 = false;
    for (auto& c : cands) if (c.w == 1920 && c.h == 1080 && c.d && c.n / (double)c.d > 29 && c.n / (double)c.d < 31) has1080p30 = true;
    Log("True 1080p30 mode exposed: %s", has1080p30 ? "yes" : "no");

    const GUID prefs[2] = { MFVideoFormat_YUY2, MFVideoFormat_NV12 };
    const Cand* chosen = nullptr;
    for (auto& pref : prefs) {
        for (auto& c : cands) {
            double fps = c.d ? c.n / (double)c.d : 0;
            if (c.sub == pref && c.w == 1920 && c.h == 1080 && fps > 59 && fps < 61) { chosen = &c; break; }
        }
        if (chosen) break;
    }
    if (!chosen) {
        Log("NO uncompressed 1080p60 YUY2/NV12 mode exposed by Media Foundation. NOT falling back to DirectShow (ticket rule). Report to user.");
        g_status = "NO 1080p60 UNCOMPRESSED MODE";
        return false;
    }
    g_fmt = (chosen->sub == MFVideoFormat_YUY2) ? FMT_YUY2 : FMT_NV12;
    Log("Chosen: native type #%lu, %s 1080p60 (preferred order YUY2 then NV12; first match in list)", (unsigned long)chosen->idx, g_fmt == FMT_YUY2 ? "YUY2" : "NV12");

    ComPtr<IMFMediaType> nt;
    reader->GetNativeMediaType(kVid, chosen->idx, &nt);
    hr = reader->SetCurrentMediaType(kVid, nullptr, nt.Get());
    if (FAILED(hr)) { Log("SetCurrentMediaType FAILED hr=0x%08lX", (unsigned long)hr); g_status = "SetCurrentMediaType failed"; return false; }
    ComPtr<IMFMediaType> cur;
    reader->GetCurrentMediaType(kVid, &cur);
    Log("Current type: %s", DescribeType(cur.Get()).c_str());
    UINT32 rng = MFGetAttributeUINT32(cur.Get(), MF_MT_VIDEO_NOMINAL_RANGE, 0);
    UINT32 mat = MFGetAttributeUINT32(cur.Get(), MF_MT_YUV_MATRIX, 0);
    g_fullRange = (rng == MFNominalRange_0_255) ? 1 : 0;
    g_m709 = (mat == MFVideoTransferMatrix_BT601) ? 0 : 1;
    Log("Colour: range=%s (media type nomRange=%u), matrix=%s (media type yuvMatrix=%u; 0/unknown -> assumed BT.709 limited)",
        g_fullRange ? "full 0-255" : "limited 16-235", rng, g_m709 ? "BT.709" : "BT.601", mat);

    g_vw = 1920; g_vh = 1080;
    g_frameBytes = (g_fmt == FMT_YUY2) ? (size_t)g_vw * g_vh * 2 : (size_t)g_vw * g_vh * 3 / 2;
    g_pending.assign(g_frameBytes, 0); g_upload.assign(g_frameBytes, 0); g_last.assign(g_frameBytes, 0);

    hr = reader->ReadSample(kVid, 0, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) { Log("First ReadSample failed hr=0x%08lX", (unsigned long)hr); g_status = "ReadSample failed"; return false; }
    g_status = "capturing";
    Log("Capture started (async callback, no converters).");
    return true;
}

// ---------------------------------------------------------------- audio
static const PROPERTYKEY kFriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

static std::wstring DevName(IMMDevice* d) {
    ComPtr<IPropertyStore> ps;
    std::wstring r;
    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) r = pv.pwszVal;
        PropVariantClear(&pv);
    }
    return r;
}

static const UINT RING_FRAMES = 1920;      // 40 ms capacity
static const UINT PRIME_FRAMES = 480;      // 10 ms before playback starts
static CRITICAL_SECTION g_ringCs;
static float g_ring[RING_FRAMES * 2];
static uint64_t g_rd = 0, g_wr = 0;
static std::atomic<uint32_t> c_underrun{0}, c_overrun{0};
static std::atomic<bool> g_audioRun{false};
static ComPtr<IAudioClient> g_capClient, g_rndClient;
static ComPtr<IAudioCaptureClient> g_capSvc;
static ComPtr<IAudioRenderClient> g_rndSvc;
static HANDLE g_capEv = nullptr, g_rndEv = nullptr, g_capThread = nullptr, g_rndThread = nullptr;
static UINT32 g_rndBufFrames = 0;
static bool g_audioOk = false;

static void RingWrite(const float* src, UINT frames, bool silent) {
    EnterCriticalSection(&g_ringCs);
    for (UINT i = 0; i < frames; i++) {
        if (g_wr - g_rd >= RING_FRAMES) { g_rd++; c_overrun++; }
        UINT pos = (UINT)(g_wr % RING_FRAMES);
        g_ring[pos * 2] = silent ? 0.f : src[i * 2];
        g_ring[pos * 2 + 1] = silent ? 0.f : src[i * 2 + 1];
        g_wr++;
    }
    LeaveCriticalSection(&g_ringCs);
}
static UINT RingFill() { EnterCriticalSection(&g_ringCs); UINT f = (UINT)(g_wr - g_rd); LeaveCriticalSection(&g_ringCs); return f; }

static DWORD WINAPI CaptureThread(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD ti = 0; HANDLE mm = AvSetMmThreadCharacteristicsW(L"Pro Audio", &ti);
    while (g_audioRun) {
        WaitForSingleObject(g_capEv, 200);
        for (;;) {
            UINT32 n = 0;
            if (FAILED(g_capSvc->GetNextPacketSize(&n)) || !n) break;
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(g_capSvc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            RingWrite((const float*)data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
            g_capSvc->ReleaseBuffer(frames);
        }
    }
    if (mm) AvRevertMmThreadCharacteristics(mm);
    return 0;
}

static DWORD WINAPI RenderThread(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD ti = 0; HANDLE mm = AvSetMmThreadCharacteristicsW(L"Pro Audio", &ti);
    bool primed = false;
    while (g_audioRun) {
        WaitForSingleObject(g_rndEv, 200);
        UINT32 pad = 0;
        if (FAILED(g_rndClient->GetCurrentPadding(&pad))) continue;
        UINT32 avail = g_rndBufFrames - pad;
        if (!avail) continue;
        BYTE* out = nullptr;
        if (FAILED(g_rndSvc->GetBuffer(avail, &out))) continue;
        float* f = (float*)out;
        EnterCriticalSection(&g_ringCs);
        UINT32 fill = (UINT32)(g_wr - g_rd);
        if (!primed && fill >= PRIME_FRAMES) primed = true;
        UINT32 i = 0;
        if (primed) {
            for (; i < avail && g_rd < g_wr; i++, g_rd++) {
                UINT pos = (UINT)(g_rd % RING_FRAMES);
                f[i * 2] = g_ring[pos * 2]; f[i * 2 + 1] = g_ring[pos * 2 + 1];
            }
            if (i < avail) { primed = false; c_underrun++; }
        }
        LeaveCriticalSection(&g_ringCs);
        for (; i < avail; i++) { f[i * 2] = 0.f; f[i * 2 + 1] = 0.f; }
        g_rndSvc->ReleaseBuffer(avail, 0);
    }
    if (mm) AvRevertMmThreadCharacteristics(mm);
    return 0;
}

static bool StartAudio() {
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) { Log("MMDeviceEnumerator failed hr=0x%08lX", (unsigned long)hr); return false; }
    ComPtr<IMMDeviceCollection> col;
    en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col);
    UINT n = 0; if (col) col->GetCount(&n);
    Log("Audio capture endpoints: %u", n);
    ComPtr<IMMDevice> pick;
    std::wstring pickName;
    for (UINT i = 0; i < n; i++) {
        ComPtr<IMMDevice> d; col->Item(i, &d);
        std::wstring nm = DevName(d.Get());
        Log("  [%u] \"%s\"", i, U8(nm.c_str()).c_str());
        if (pick) continue;
        bool match = false;
        if (!g_videoName.empty() && ContainsI(nm, g_videoName.c_str())) match = true;          // full video name inside endpoint name
        if (ContainsI(nm, L"gc553") || ContainsI(nm, L"live gamer") || ContainsI(nm, L"avermedia")) match = true;
        if (match) { pick = d; pickName = nm; Log("  -> matched by device name (video name / GC553 / Live Gamer / AVerMedia)"); }
    }
    ComPtr<IMMDevice> defOut;
    en->GetDefaultAudioEndpoint(eRender, eConsole, &defOut);
    if (defOut) Log("Default render device: \"%s\"", U8(DevName(defOut.Get()).c_str()).c_str());
    if (!pick) { Log("No audio capture endpoint matched the video device. No sound."); return false; }
    if (!defOut) { Log("No default render device. No sound."); return false; }
    Log("Chosen audio capture endpoint: \"%s\"", U8(pickName.c_str()).c_str());

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; wf.nChannels = 2; wf.nSamplesPerSec = 48000;
    wf.wBitsPerSample = 32; wf.nBlockAlign = 8; wf.nAvgBytesPerSec = 48000 * 8;
    DWORD fl = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    hr = pick->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_capClient);
    if (FAILED(hr)) { Log("Capture endpoint Activate failed hr=0x%08lX (in use / privacy blocked?)", (unsigned long)hr); return false; }
    hr = g_capClient->Initialize(AUDCLNT_SHAREMODE_SHARED, fl, 0, 0, &wf, nullptr);
    if (FAILED(hr)) { Log("Capture Initialize failed hr=0x%08lX", (unsigned long)hr); return false; }
    hr = defOut->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_rndClient);
    if (FAILED(hr)) { Log("Render Activate failed hr=0x%08lX", (unsigned long)hr); return false; }
    hr = g_rndClient->Initialize(AUDCLNT_SHAREMODE_SHARED, fl, 0, 0, &wf, nullptr);
    if (FAILED(hr)) { Log("Render Initialize failed hr=0x%08lX", (unsigned long)hr); return false; }

    REFERENCE_TIME defP = 0, minP = 0;
    g_capClient->GetDevicePeriod(&defP, &minP);
    Log("Capture device period default=%.2f ms min=%.2f ms", defP / 10000.0, minP / 10000.0);
    defP = 0; minP = 0;
    g_rndClient->GetDevicePeriod(&defP, &minP);
    g_rndClient->GetBufferSize(&g_rndBufFrames);
    Log("Render device period default=%.2f ms min=%.2f ms, buffer=%u frames (%.1f ms)", defP / 10000.0, minP / 10000.0, g_rndBufFrames, g_rndBufFrames / 48.0);
    g_capClient->GetService(IID_PPV_ARGS(&g_capSvc));
    g_rndClient->GetService(IID_PPV_ARGS(&g_rndSvc));
    g_capEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_rndEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_capClient->SetEventHandle(g_capEv);
    g_rndClient->SetEventHandle(g_rndEv);
    BYTE* b = nullptr;
    if (SUCCEEDED(g_rndSvc->GetBuffer(g_rndBufFrames, &b))) g_rndSvc->ReleaseBuffer(g_rndBufFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    g_audioRun = true;
    g_capThread = CreateThread(nullptr, 0, CaptureThread, nullptr, 0, nullptr);
    g_rndThread = CreateThread(nullptr, 0, RenderThread, nullptr, 0, nullptr);
    hr = g_capClient->Start();
    if (FAILED(hr)) Log("Capture Start failed hr=0x%08lX", (unsigned long)hr);
    hr = g_rndClient->Start();
    if (FAILED(hr)) Log("Render Start failed hr=0x%08lX", (unsigned long)hr);
    g_audioOk = true;
    Log("Audio started: ring %u ms capacity, plays after %u ms primed.", RING_FRAMES / 48, PRIME_FRAMES / 48);
    return true;
}

static void StopAudio() {
    g_audioRun = false;
    if (g_capThread) { WaitForSingleObject(g_capThread, 1000); CloseHandle(g_capThread); }
    if (g_rndThread) { WaitForSingleObject(g_rndThread, 1000); CloseHandle(g_rndThread); }
    if (g_capClient) g_capClient->Stop();
    if (g_rndClient) g_rndClient->Stop();
}

// ---------------------------------------------------------------- D3D11
static HWND g_hwnd;
static ComPtr<ID3D11Device> g_dev;
static ComPtr<ID3D11DeviceContext> g_ctx;
static ComPtr<IDXGISwapChain2> g_sc;
static ComPtr<ID3D11RenderTargetView> g_rtv;
static ComPtr<ID3D11VertexShader> g_vs;
static ComPtr<ID3D11PixelShader> g_ps;
static ComPtr<ID3D11Buffer> g_cbuf;
static ComPtr<ID3D11Texture2D> g_tex0, g_tex1;
static ComPtr<ID3D11ShaderResourceView> g_srv0, g_srv1;
static HANDLE g_waitable = nullptr;
static bool g_tearing = false;
static UINT g_scFlags = 0;
static bool g_haveVideo = false;

static const char* kShader = R"(
cbuffer C : register(b0) { float2 dim; uint fmt; uint fullRange; uint m709; uint3 pad; };
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs(uint id : SV_VertexID) {
    VO o; float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
}
float4 ps(VO i) : SV_Target {
    int2 p = clamp(int2(i.uv * dim), int2(0, 0), int2(dim) - 1);
    float y, u, v;
    if (fmt == 0) {            // YUY2: texel = Y0 U Y1 V
        float4 t = t0.Load(int3(p.x >> 1, p.y, 0));
        y = (p.x & 1) ? t.b : t.r; u = t.g; v = t.a;
    } else {                   // NV12
        y = t0.Load(int3(p, 0)).r;
        float2 c = t1.Load(int3(p >> 1, 0)).rg; u = c.x; v = c.y;
    }
    if (fullRange == 0) { y = (y - 16.0/255.0) * (255.0/219.0); u = (u - 128.0/255.0) * (255.0/224.0); v = (v - 128.0/255.0) * (255.0/224.0); }
    else { u -= 0.5; v -= 0.5; }
    float3 rgb;
    if (m709 != 0) rgb = float3(y + 1.5748*v, y - 0.1873*u - 0.4681*v, y + 1.8556*u);
    else rgb = float3(y + 1.402*v, y - 0.3441*u - 0.7141*v, y + 1.772*u);
    return float4(saturate(rgb), 1);
}
)";

// ---- on-screen fps overlay: tiny built-in 5x7 font, drawn only inside frames that are presented anyway
static std::atomic<bool> g_osd{true};
static const int OSD_W = 9 * 6 + 1, OSD_H = 9;   // up to "120 / 120"
static uint8_t g_osdPix[OSD_W * OSD_H];
static bool g_osdDirty = true;
static ComPtr<ID3D11Texture2D> g_osdTex;
static ComPtr<ID3D11ShaderResourceView> g_osdSrv;
static ComPtr<ID3D11PixelShader> g_psOsd;
static ComPtr<ID3D11BlendState> g_blend;

static const char* kOsdShader = R"(
Texture2D<float> t0 : register(t0);
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 ps(VO i) : SV_Target {
    float v = t0.Load(int3(clamp(int2(i.uv * float2(55, 9)), int2(0, 0), int2(54, 8)), 0));
    return float4(v, v, v, 0.55 + 0.45 * v);
}
)";

static const char* kGlyph[12][7] = {
    {"01110","10001","10011","10101","11001","10001","01110"}, {"00100","01100","00100","00100","00100","00100","01110"},
    {"01110","10001","00001","00010","00100","01000","11111"}, {"11110","00001","00001","01110","00001","00001","11110"},
    {"00010","00110","01010","10010","11111","00010","00010"}, {"11111","10000","11110","00001","00001","10001","01110"},
    {"00110","01000","10000","11110","10001","10001","01110"}, {"11111","00001","00010","00100","01000","01000","01000"},
    {"01110","10001","10001","01110","10001","10001","01110"}, {"01110","10001","10001","01111","00001","00010","01100"},
    {"00001","00010","00010","00100","01000","01000","10000"}, {"00000","00000","00000","00000","00000","00000","00000"}};

static void BuildOsd(unsigned u, unsigned a) {
    char s[32];
    snprintf(s, sizeof s, "%u / %u", u, a);
    memset(g_osdPix, 0, sizeof g_osdPix);
    int x = 1;
    for (const char* c = s; *c && x + 5 <= OSD_W; c++, x += 6) {
        int gi = (*c >= '0' && *c <= '9') ? *c - '0' : (*c == '/') ? 10 : 11;
        for (int r = 0; r < 7; r++)
            for (int q = 0; q < 5; q++)
                if (kGlyph[gi][r][q] == '1') g_osdPix[(r + 1) * OSD_W + x + q] = 255;
    }
    g_osdDirty = true;
}

static void CreateRtv() {
    ComPtr<ID3D11Texture2D> bb;
    g_sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb.Get(), nullptr, &g_rtv);
}

static bool InitD3D() {
    UINT fl = 0;
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, fl, nullptr, 0, D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) { Log("D3D11CreateDevice failed hr=0x%08lX", (unsigned long)hr); return false; }
    ComPtr<IDXGIDevice> dd; g_dev.As(&dd);
    ComPtr<IDXGIAdapter> ad; dd->GetAdapter(&ad);
    DXGI_ADAPTER_DESC adesc; ad->GetDesc(&adesc);
    Log("D3D11 device on adapter \"%s\"", U8(adesc.Description).c_str());
    ComPtr<IDXGIFactory5> fac;
    ad->GetParent(IID_PPV_ARGS(&fac));
    BOOL tear = FALSE;
    if (FAILED(fac->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tear, sizeof tear))) tear = FALSE;
    g_tearing = tear != FALSE;
    Log("DXGI tearing (ALLOW_TEARING) supported: %s", g_tearing ? "yes" : "NO");

    g_scFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (g_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = 0; sd.Height = 0;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = g_scFlags;
    ComPtr<IDXGISwapChain1> sc1;
    hr = fac->CreateSwapChainForHwnd(g_dev.Get(), g_hwnd, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) { Log("CreateSwapChainForHwnd failed hr=0x%08lX", (unsigned long)hr); return false; }
    fac->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&g_sc);
    g_sc->SetMaximumFrameLatency(1);
    g_waitable = g_sc->GetFrameLatencyWaitableObject();
    Log("Swap chain: FLIP_DISCARD, 2 buffers, flags=0x%X, max frame latency 1", g_scFlags);
    CreateRtv();

    ComPtr<ID3DBlob> vb, pb, err;
    hr = D3DCompile(kShader, strlen(kShader), "shader", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vb, &err);
    if (FAILED(hr)) { Log("VS compile failed: %s", err ? (char*)err->GetBufferPointer() : "?"); return false; }
    hr = D3DCompile(kShader, strlen(kShader), "shader", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &pb, &err);
    if (FAILED(hr)) { Log("PS compile failed: %s", err ? (char*)err->GetBufferPointer() : "?"); return false; }
    g_dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &g_vs);
    g_dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &g_ps);
    {
        ComPtr<ID3DBlob> ob;
        if (SUCCEEDED(D3DCompile(kOsdShader, strlen(kOsdShader), "osd", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ob, &err))) {
            g_dev->CreatePixelShader(ob->GetBufferPointer(), ob->GetBufferSize(), nullptr, &g_psOsd);
            D3D11_TEXTURE2D_DESC od = {};
            od.Width = OSD_W; od.Height = OSD_H; od.MipLevels = 1; od.ArraySize = 1; od.Format = DXGI_FORMAT_R8_UNORM;
            od.SampleDesc.Count = 1; od.Usage = D3D11_USAGE_DEFAULT; od.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            g_dev->CreateTexture2D(&od, nullptr, &g_osdTex);
            g_dev->CreateShaderResourceView(g_osdTex.Get(), nullptr, &g_osdSrv);
            D3D11_BLEND_DESC bl = {};
            bl.RenderTarget[0].BlendEnable = TRUE;
            bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            g_dev->CreateBlendState(&bl, &g_blend);
        } else Log("OSD shader compile failed; overlay disabled");
    }

    if (g_haveVideo) {
        struct { float w, h; UINT fmt, full, m709, p[3]; } cbd = { (float)g_vw, (float)g_vh, (UINT)g_fmt, g_fullRange, g_m709, {0, 0, 0} };
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 32; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA sdat = { &cbd, 0, 0 };
        g_dev->CreateBuffer(&bd, &sdat, &g_cbuf);
        D3D11_TEXTURE2D_DESC td = {};
        td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (g_fmt == FMT_YUY2) {
            td.Width = g_vw / 2; td.Height = g_vh; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            g_dev->CreateTexture2D(&td, nullptr, &g_tex0);
        } else {
            td.Width = g_vw; td.Height = g_vh; td.Format = DXGI_FORMAT_R8_UNORM;
            g_dev->CreateTexture2D(&td, nullptr, &g_tex0);
            td.Width = g_vw / 2; td.Height = g_vh / 2; td.Format = DXGI_FORMAT_R8G8_UNORM;
            g_dev->CreateTexture2D(&td, nullptr, &g_tex1);
            g_dev->CreateShaderResourceView(g_tex1.Get(), nullptr, &g_srv1);
        }
        g_dev->CreateShaderResourceView(g_tex0.Get(), nullptr, &g_srv0);
    }
    return true;
}

static void Draw(bool newFrame) {
    if (!g_rtv) return;
    if (newFrame && g_haveVideo) {
        if (g_fmt == FMT_YUY2) {
            g_ctx->UpdateSubresource(g_tex0.Get(), 0, nullptr, g_upload.data(), g_vw * 2, 0);
        } else {
            g_ctx->UpdateSubresource(g_tex0.Get(), 0, nullptr, g_upload.data(), g_vw, 0);
            g_ctx->UpdateSubresource(g_tex1.Get(), 0, nullptr, g_upload.data() + (size_t)g_vw * g_vh, g_vw, 0);
        }
    }
    RECT rc; GetClientRect(g_hwnd, &rc);
    float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    float vw = cw, vh = cw * 9.f / 16.f;
    if (vh > ch) { vh = ch; vw = ch * 16.f / 9.f; }
    D3D11_VIEWPORT vp = { (cw - vw) / 2, (ch - vh) / 2, vw, vh, 0, 1 };
    float black[4] = { 0, 0, 0, 1 };
    ID3D11RenderTargetView* rtv = g_rtv.Get();
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    g_ctx->ClearRenderTargetView(rtv, black);
    if (g_haveVideo) {
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->VSSetShader(g_vs.Get(), nullptr, 0);
        g_ctx->PSSetShader(g_ps.Get(), nullptr, 0);
        ID3D11Buffer* cb = g_cbuf.Get(); g_ctx->PSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srvs[2] = { g_srv0.Get(), g_srv1.Get() };
        g_ctx->PSSetShaderResources(0, 2, srvs);
        g_ctx->Draw(3, 0);
        if (g_osd && g_psOsd && g_osdTex) {
            if (g_osdDirty) { g_ctx->UpdateSubresource(g_osdTex.Get(), 0, nullptr, g_osdPix, OSD_W, 0); g_osdDirty = false; }
            float sc = (float)(int)(vh / 180.f); if (sc < 2.f) sc = 2.f;
            D3D11_VIEWPORT ov = { vp.TopLeftX + 10.f, vp.TopLeftY + 10.f, OSD_W * sc, OSD_H * sc, 0, 1 };
            g_ctx->RSSetViewports(1, &ov);
            g_ctx->PSSetShader(g_psOsd.Get(), nullptr, 0);
            ID3D11ShaderResourceView* os = g_osdSrv.Get(); g_ctx->PSSetShaderResources(0, 1, &os);
            g_ctx->OMSetBlendState(g_blend.Get(), nullptr, 0xFFFFFFFF);
            g_ctx->Draw(3, 0);
            g_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        }
    }
    g_sc->Present(0, g_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
    c_presented++;
}

static void OnResize() {
    if (!g_sc) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g_rtv.Reset();
    HRESULT hr = g_sc->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, g_scFlags);
    if (FAILED(hr)) { Log("ResizeBuffers failed hr=0x%08lX", (unsigned long)hr); return; }
    CreateRtv();
    Draw(false);
}

// ---------------------------------------------------------------- window
static bool g_full = false;
static WINDOWPLACEMENT g_wp = { sizeof g_wp };

static void ToggleFullscreen() {
    if (!g_full) {
        GetWindowPlacement(g_hwnd, &g_wp);
        MONITORINFO mi = { sizeof mi };
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_full = true;
        Log("Borderless fullscreen ON (never exclusive)");
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(g_hwnd, &g_wp);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        g_full = false;
        Log("Borderless fullscreen OFF");
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: if (w != SIZE_MINIMIZED) OnResize(); return 0;
    case WM_KEYDOWN:
        if (w == VK_F11) ToggleFullscreen();
        else if (w == VK_ESCAPE && g_full) ToggleFullscreen();
        else if (w == 'D') { bool v = !g_drop.load(); g_drop = v; Log("Repeated Frame dropping: %s", v ? "ON" : "OFF"); }
        else if (w == 'F') { bool v = !g_osd.load(); g_osd = v; Log("FPS overlay: %s (shows on next presented frame)", v ? "ON" : "OFF"); }
        else if (w == 'T') { bool v = !g_tol.load(); g_tol = v; Log("Near-duplicate tolerance: %s", v ? "ON" : "OFF"); }
        return 0;
    case WM_DESTROY: g_quit = true; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void Stats(double secs) {
    uint32_t a = c_arrived.exchange(0), u = c_unique.exchange(0), r = c_repeated.exchange(0), nr = c_near.exchange(0),
             rp = c_replaced.exchange(0), pr = c_presented.exchange(0), uf = c_underrun.exchange(0), of = c_overrun.exchange(0);
    UINT fill = g_audioOk ? RingFill() : 0;
    double k = 1.0 / secs;
    Log("STATS arrived=%.1f/s unique=%.1f/s repeated=%.1f/s nearDup=%.1f/s replaced=%.1f/s presented=%.1f/s | audio ring fill=%.1f ms underruns=%u overruns=%u | drop=%s tol=%s",
        a * k, u * k, r * k, nr * k, rp * k, pr * k, fill / 48.0, uf, of, g_drop ? "on" : "off", g_tol ? "on" : "off");
    BuildOsd((unsigned)(u * k + 0.5), (unsigned)(a * k + 0.5));
    wchar_t t[256];
    swprintf(t, 256, L"GamePlayer PROTOTYPE | arrived %.0f  unique %.0f | drop %s (D) | fps overlay %s (F) | %S%S",
             a * k, u * k, g_drop ? L"ON" : L"OFF", g_osd ? L"ON" : L"OFF", g_status.c_str(), g_audioOk ? "" : " | no audio");
    SetWindowTextW(g_hwnd, t);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    InitializeCriticalSection(&g_logCs);
    InitializeCriticalSection(&g_frameCs);
    InitializeCriticalSection(&g_ringCs);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring lp = path;
    lp = lp.substr(0, lp.find_last_of(L'\\') + 1) + L"gameplayer-prototype.log";
    _wfopen_s(&g_logFile, lp.c_str(), L"w");
    Log("GamePlayer prototype (ticket 01, throwaway) starting. Log: %s", U8(lp.c_str()).c_str());

    int runSecs = 0;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; i++) if (!wcscmp(argv[i], L"--run")) runSecs = _wtoi(argv[i + 1]);
    if (runSecs) Log("Auto-exit after %d s (--run)", runSecs);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    g_frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"GPProto";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_hwnd = CreateWindowW(L"GPProto", L"GamePlayer PROTOTYPE", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, hi, nullptr);

    g_haveVideo = StartCapture();
    if (g_haveVideo) StartAudio(); else Log("Video not running; audio not started.");
    if (!InitD3D()) { Log("D3D init failed; exiting."); g_quit = true; }


    LARGE_INTEGER fq, t0, last;
    QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0); last = t0;
    MSG msg;
    bool done = g_quit;
    while (!done) {
        MsgWaitForMultipleObjects(1, &g_frameEvent, FALSE, 50, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        bool have = false;
        EnterCriticalSection(&g_frameCs);
        if (g_havePending) { g_pending.swap(g_upload); g_havePending = false; have = true; }
        LeaveCriticalSection(&g_frameCs);
        if (have && g_haveVideo && g_rtv) {
            if (g_waitable) WaitForSingleObjectEx(g_waitable, 100, TRUE);
            Draw(true);
        }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - last.QuadPart) / fq.QuadPart;
        if (dt >= 1.0) { Stats(dt); last = now; }
        if (runSecs && (double)(now.QuadPart - t0.QuadPart) / fq.QuadPart >= runSecs) done = true;
    }

    Log("Shutting down.");
    g_quit = true;
    StopAudio();
    if (g_source) g_source->Shutdown();
    Sleep(100);
    if (g_cb) { g_cb->reader.Reset(); g_cb->Release(); }
    g_source.Reset();
    MFShutdown();
    Log("Done.");
    if (g_logFile) fclose(g_logFile);
    return 0;
}
