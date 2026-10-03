// SMP — Simple Media Player (GUI edition)
// A minimal media player based on native Windows APIs.
//
// Build (MSYS2 MINGW64):
//   gcc smp.c -o smp.exe -municode -mwindows -DUNICODE -D_UNICODE \
//       -lole32 -lmfplat -lmfreadwrite -lmfuuid -lwinmm -lxaudio2_8 \
//       -lgdi32 -luser32 -luuid -lksuser -lshlwapi -lavrt \
//       -lcomdlg32 -lshell32 \
//       -std=c11 -O2 -s
//
// Logs are written next to the executable as "smp.log".
// Settings are stored next to the executable as "smp.ini".
//
// Supported: MP3 / MP4 (H.264 + AAC) / WAV (16-bit PCM) / MSM
//
// This software uses minimp3 (CC0 1.0 Universal). https://github.com/lieff/minimp3
//
// Copyright (c) 2026 SimpleToolsStudio
// SPDX-License-Identifier: MIT

#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <xaudio2.h>
#include <propidl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "minimsm.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "xaudio2_8.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

HRESULT WINAPI MFGetAttributeSize(IMFAttributes*, REFGUID, UINT32*, UINT32*);

/* ====================================================================== */
/* Forward declarations                                                   */
/* ====================================================================== */

static DWORD WINAPI AudioThreadProc(LPVOID param);
static DWORD WINAPI VideoThreadProc(LPVOID param);
static void SeekTo(double t);
static void StopPlaybackAll(void);
static void OnPlaybackEnded(void);
static void PlayNewFile(const wchar_t* path);
static void PlaylistPlay(int idx);

/* ====================================================================== */
/* Logging                                                                */
/* ====================================================================== */

static FILE*            g_logFile     = NULL;
static CRITICAL_SECTION g_logLock;
static int              g_logLockInit = 0;

static void LogOpen(void) {
    if (g_logFile) return;
    if (!g_logLockInit) { InitializeCriticalSection(&g_logLock); g_logLockInit = 1; }

    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        g_logFile = fopen("smp.log", "w");
    } else {
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (slash) *(slash + 1) = L'\0';
        wcscat(exePath, L"smp.log");
        g_logFile = _wfopen(exePath, L"w");
    }
    if (g_logFile) setvbuf(g_logFile, NULL, _IOLBF, 0);
}

static void LogClose(void) {
    if (g_logFile) { fclose(g_logFile); g_logFile = NULL; }
    if (g_logLockInit) { DeleteCriticalSection(&g_logLock); g_logLockInit = 0; }
}

static void Log(const char* fmt, ...) {
    if (!g_logFile) return;
    EnterCriticalSection(&g_logLock);

    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_logFile, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap; va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);

    fputc('\n', g_logFile);
    fflush(g_logFile);

    LeaveCriticalSection(&g_logLock);
}

/* ====================================================================== */
/* Media types and global player state                                    */
/* ====================================================================== */

typedef enum { MEDIA_NONE, MEDIA_MP4, MEDIA_MP3, MEDIA_WAV, MEDIA_MSM } MediaType;

typedef struct {
    MediaType type;
    wchar_t   path[MAX_PATH];
    IMFSourceReader* vReader;
    int   vWidth, vHeight;
    IMFSourceReader* aReader;
    int   aSampleRate, aChannels;
    uint8_t* audioData;
    size_t   audioSize;
    size_t   audioPos;
    LARGE_INTEGER qpcFreq, qpcStart;
    double  audioClock;
    volatile int paused;
    volatile int quit;
} Player;

static Player g_p;

/* ---- XAudio2 -------------------------------------------------------- */

static IXAudio2*               g_xaudio = NULL;
static IXAudio2MasteringVoice* g_master = NULL;
static IXAudio2SourceVoice*    g_source = NULL;
static volatile int g_audioThreadRunning = 0;
static HANDLE       g_hAudioThread = NULL;

static volatile int g_videoThreadRunning = 0;
static HANDLE       g_hVideoThread = NULL;

typedef struct BufNode { uint8_t* p; struct BufNode* next; } BufNode;
static BufNode* g_bufList = NULL;
static CRITICAL_SECTION g_bufLock;

/* ---- Video frame buffer --------------------------------------------- */

static HWND     g_hwnd = NULL;
static uint8_t* g_frameBuf = NULL;
static int      g_frameW = 0, g_frameH = 0;
static BITMAPINFO g_bmi;

/* ---- Playback state ------------------------------------------------- */

static int    g_playing = 0;
static int    g_isVideo = 0;
static int    g_eof     = 0;
static double g_totalSec = 0.0;
static float  g_volume  = 1.0f;
static wchar_t g_curPath[MAX_PATH];
static wchar_t g_title[512]   = L"SMP - Simple Media Player";
static wchar_t g_status[512]  = L"Open a media file (drag & drop, or click \"Open\")";

/* ---- Playlist ------------------------------------------------------- */

#define MAX_PLAYLIST 512
static wchar_t* g_playlist[MAX_PLAYLIST];
static int      g_playlistCount  = 0;
static int      g_playlistCur    = -1;
static int      g_playlistScroll = 0;
static int      g_playlistHot    = -1;

/* ---- Drag preview for progress bar ---------------------------------- */

static double g_dragPreview = -1.0;

/* ---- Settings (persisted) ------------------------------------------- */

static wchar_t g_iniPath[MAX_PATH];
static wchar_t g_lastDir[MAX_PATH];
static int     g_winX = CW_USEDEFAULT, g_winY = CW_USEDEFAULT;
static int     g_winW = 1100, g_winH = 620;

/* ---- UI ------------------------------------------------------------- */

#define UI_PANEL_H    96
#define UI_PLAYLIST_W 220
#define UI_ITEM_H     26
#define BTN_COUNT     7

typedef enum {
    BTN_OPEN = 0, BTN_PREV, BTN_PLAY, BTN_STOP,
    BTN_NEXT, BTN_BACK, BTN_FWD
} BtnId;

typedef struct { RECT rc; BtnId id; const wchar_t* text; int hot; int down; } Button;

static Button g_btns[BTN_COUNT];
static RECT   g_rcProgress;
static RECT   g_rcClient;
static RECT   g_rcPlaylist;
static int    g_dragging  = 0;
static HFONT  g_fontUI    = NULL;
static HFONT  g_fontInfo  = NULL;

/* ====================================================================== */
/* Small utilities                                                        */
/* ====================================================================== */

static const char* WideToUtf8(const wchar_t* w) {
    static char buf[2048];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), NULL, NULL);
    return buf;
}

static void FmtTime(double sec, wchar_t* out, size_t n) {
    if (sec < 0) sec = 0;
    int t = (int)(sec + 0.5);
    _snwprintf(out, n, L"%02d:%02d", t / 60, t % 60);
}

static double GetTotalSec(void) {
    if (g_p.type == MEDIA_MP4) return g_totalSec;
    if (g_p.audioData && g_p.aSampleRate > 0 && g_p.aChannels > 0)
        return (double)g_p.audioSize / (g_p.aSampleRate * g_p.aChannels * 2);
    return 0.0;
}

static int PtInRectX(POINT p, RECT r) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

static int IsAudioOnly(void) {
    return g_p.type == MEDIA_MP3 || g_p.type == MEDIA_WAV || g_p.type == MEDIA_MSM;
}

static void ShowError(const wchar_t* msg) {
    Log("ERROR: %s", WideToUtf8(msg));
    MessageBoxW(g_hwnd, msg, L"SMP", MB_OK | MB_ICONWARNING);
}

/* ====================================================================== */
/* Settings (smp.ini next to the exe)                                     */
/* ====================================================================== */

static void InitIniPath(void) {
    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        wcscpy(g_iniPath, L"smp.ini");
        return;
    }
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    _snwprintf(g_iniPath, MAX_PATH, L"%ssmp.ini", exePath);
}

static void SettingsLoad(void) {
    InitIniPath();
    g_volume = (float)GetPrivateProfileIntW(L"player", L"volume", 100, g_iniPath) / 100.0f;
    if (g_volume < 0) g_volume = 0;
    if (g_volume > 1) g_volume = 1;

    g_winX = GetPrivateProfileIntW(L"window", L"x", CW_USEDEFAULT, g_iniPath);
    g_winY = GetPrivateProfileIntW(L"window", L"y", CW_USEDEFAULT, g_iniPath);
    g_winW = GetPrivateProfileIntW(L"window", L"w", 1100, g_iniPath);
    g_winH = GetPrivateProfileIntW(L"window", L"h", 620,  g_iniPath);

    g_lastDir[0] = 0;
    GetPrivateProfileStringW(L"player", L"lastdir", L"", g_lastDir, MAX_PATH, g_iniPath);

    Log("Settings loaded: vol=%d%% win=%d,%d %dx%d dir=%s",
        (int)(g_volume * 100), g_winX, g_winY, g_winW, g_winH,
        WideToUtf8(g_lastDir));
}

static void SettingsSave(void) {
    if (!g_iniPath[0]) InitIniPath();

    wchar_t buf[32];
    _snwprintf(buf, 32, L"%d", (int)(g_volume * 100 + 0.5f));
    WritePrivateProfileStringW(L"player", L"volume", buf, g_iniPath);

    if (g_hwnd) {
        WINDOWPLACEMENT wp; memset(&wp, 0, sizeof(wp));
        wp.length = sizeof(wp);
        if (GetWindowPlacement(g_hwnd, &wp)) {
            RECT r = wp.rcNormalPosition;
            _snwprintf(buf, 32, L"%d", r.left);   WritePrivateProfileStringW(L"window", L"x", buf, g_iniPath);
            _snwprintf(buf, 32, L"%d", r.top);    WritePrivateProfileStringW(L"window", L"y", buf, g_iniPath);
            _snwprintf(buf, 32, L"%d", r.right - r.left); WritePrivateProfileStringW(L"window", L"w", buf, g_iniPath);
            _snwprintf(buf, 32, L"%d", r.bottom - r.top); WritePrivateProfileStringW(L"window", L"h", buf, g_iniPath);
        }
    }

    if (g_lastDir[0]) {
        WritePrivateProfileStringW(L"player", L"lastdir", g_lastDir, g_iniPath);
    }
    Log("Settings saved");
}

/* ====================================================================== */
/* WAV parsing                                                            */
/* ====================================================================== */

typedef struct {
    uint16_t audioFormat, channels, bitsPerSample;
    uint32_t sampleRate;
    const uint8_t* data;
    size_t dataSize;
} WavInfo;

static int ParseWav(const uint8_t* buf, size_t size, WavInfo* out) {
    if (size < 44 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) return 0;
    size_t pos = 12;
    int ff = 0, fd = 0;
    while (pos + 8 <= size) {
        const char* id = (const char*)(buf + pos);
        uint32_t cs = *(const uint32_t*)(buf + pos + 4);
        pos += 8;
        if (!memcmp(id, "fmt ", 4) && cs >= 16) {
            out->audioFormat = *(const uint16_t*)(buf + pos);
            out->channels    = *(const uint16_t*)(buf + pos + 2);
            out->sampleRate  = *(const uint32_t*)(buf + pos + 4);
            out->bitsPerSample = *(const uint16_t*)(buf + pos + 14);
            ff = 1;
        } else if (!memcmp(id, "data", 4)) {
            out->data = buf + pos; out->dataSize = cs; fd = 1;
        }
        pos += cs + (cs & 1);
    }
    return ff && fd;
}

/* ====================================================================== */
/* XAudio2 helpers                                                        */
/* ====================================================================== */

static int AudioInit(int sr, int ch) {
    if (FAILED(XAudio2Create(&g_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        Log("AudioInit: XAudio2Create failed");
        return 0;
    }
    if (FAILED(IXAudio2_CreateMasteringVoice(g_xaudio, &g_master, ch, sr, 0, NULL, NULL, AudioCategory_Media))) {
        Log("AudioInit: CreateMasteringVoice failed");
        return 0;
    }
    WAVEFORMATEX wfx; memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)ch;
    wfx.nSamplesPerSec  = sr;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = wfx.nChannels * 2;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    if (FAILED(IXAudio2_CreateSourceVoice(g_xaudio, &g_source, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL))) {
        Log("AudioInit: CreateSourceVoice failed");
        return 0;
    }
    InitializeCriticalSection(&g_bufLock);
    Log("AudioInit ok: %dHz %dch", sr, ch);
    return 1;
}

static void AudioShutdown(void) {
    if (g_source) { IXAudio2SourceVoice_DestroyVoice(g_source); g_source = NULL; }
    if (g_master) { IXAudio2MasteringVoice_DestroyVoice(g_master); g_master = NULL; }
    if (g_xaudio) { IXAudio2_Release(g_xaudio); g_xaudio = NULL; }
    EnterCriticalSection(&g_bufLock);
    BufNode* p = g_bufList;
    while (p) { BufNode* n = p->next; free(p->p); free(p); p = n; }
    g_bufList = NULL;
    LeaveCriticalSection(&g_bufLock);
    DeleteCriticalSection(&g_bufLock);
}

static int AudioSubmitCopy(const uint8_t* pcm, size_t bytes) {
    if (!g_source) return 0;
    XAUDIO2_VOICE_STATE st;
    for (int i = 0; i < 5000; ++i) {
        IXAudio2SourceVoice_GetState(g_source, &st, 0);
        if (st.BuffersQueued < 16) break;
        Sleep(1);
    }
    uint8_t* copy = (uint8_t*)malloc(bytes);
    memcpy(copy, pcm, bytes);
    BufNode* node = (BufNode*)malloc(sizeof(BufNode));
    node->p = copy;
    EnterCriticalSection(&g_bufLock);
    node->next = g_bufList;
    g_bufList = node;
    LeaveCriticalSection(&g_bufLock);

    XAUDIO2_BUFFER buf; memset(&buf, 0, sizeof(buf));
    buf.AudioBytes = (UINT32)bytes;
    buf.pAudioData = copy;
    buf.Flags = 0;
    return SUCCEEDED(IXAudio2SourceVoice_SubmitSourceBuffer(g_source, &buf, NULL));
}

static void ApplyVolume(void) {
    if (g_master) IXAudio2MasteringVoice_SetVolume(g_master, g_volume, XAUDIO2_COMMIT_NOW);
}

/* ====================================================================== */
/* Seeking                                                                */
/* ====================================================================== */

static void SeekTo(double t) {
    if (t < 0) t = 0;

    if (g_p.type == MEDIA_MP4) {
        PROPVARIANT var;
        if (g_p.vReader) {
            PropVariantInit(&var); var.vt = VT_I8;
            var.hVal.QuadPart = (LONGLONG)(t * 10000000.0);
            IMFSourceReader_SetCurrentPosition(g_p.vReader, &GUID_NULL, &var);
            PropVariantClear(&var);
        }
        if (g_p.aReader) {
            PropVariantInit(&var); var.vt = VT_I8;
            var.hVal.QuadPart = (LONGLONG)(t * 10000000.0);
            IMFSourceReader_SetCurrentPosition(g_p.aReader, &GUID_NULL, &var);
            PropVariantClear(&var);
        }
        if (g_source) IXAudio2SourceVoice_FlushSourceBuffers(g_source);
    } else if (g_p.audioData) {
        size_t bp = (size_t)(t * g_p.aSampleRate * g_p.aChannels * 2);
        if (bp > g_p.audioSize) bp = g_p.audioSize;
        g_p.audioPos   = bp;
        g_p.audioClock = t;
        if (g_source) IXAudio2SourceVoice_FlushSourceBuffers(g_source);
    }
    g_eof = 0;
    Log("SeekTo %.3f", t);
}

/* ====================================================================== */
/* Playlist                                                               */
/* ====================================================================== */

static void PlaylistClear(void) {
    for (int i = 0; i < g_playlistCount; ++i) {
        free(g_playlist[i]);
        g_playlist[i] = NULL;
    }
    g_playlistCount = 0;
    g_playlistCur   = -1;
    g_playlistScroll = 0;
    g_playlistHot   = -1;
}

static int PlaylistContains(const wchar_t* path) {
    for (int i = 0; i < g_playlistCount; ++i) {
        if (!_wcsicmp(g_playlist[i], path)) return 1;
    }
    return 0;
}

static void PlaylistAdd(const wchar_t* path) {
    if (g_playlistCount >= MAX_PLAYLIST) return;
    if (PlaylistContains(path)) return;
    g_playlist[g_playlistCount++] = _wcsdup(path);
    Log("Playlist add [%d]: %s", g_playlistCount - 1, WideToUtf8(path));
}

static void PlaylistPlay(int idx) {
    if (idx < 0 || idx >= g_playlistCount) return;
    g_playlistCur = idx;
    PlayNewFile(g_playlist[idx]);
}

static void PlaylistNext(void) {
    if (g_playlistCount == 0) return;
    int n = (g_playlistCur + 1) % g_playlistCount;
    PlaylistPlay(n);
}

static void PlaylistPrev(void) {
    if (g_playlistCount == 0) return;
    int p = g_playlistCur - 1;
    if (p < 0) p = g_playlistCount - 1;
    PlaylistPlay(p);
}

static void PlaylistEnsureVisible(int idx) {
    if (idx < 0) return;
    int itemsPerPage = (g_rcPlaylist.bottom - g_rcPlaylist.top - 28) / UI_ITEM_H;
    if (itemsPerPage < 1) itemsPerPage = 1;
    if (idx < g_playlistScroll) g_playlistScroll = idx;
    if (idx >= g_playlistScroll + itemsPerPage)
        g_playlistScroll = idx - itemsPerPage + 1;
    if (g_playlistScroll < 0) g_playlistScroll = 0;
}

/* ====================================================================== */
/* UI drawing                                                             */
/* ====================================================================== */

static void DrawUI(HDC dc) {
    GetClientRect(g_hwnd, &g_rcClient);
    int W = g_rcClient.right;
    int H = g_rcClient.bottom;
    int videoH = H - UI_PANEL_H;
    if (videoH < 0) videoH = 0;

    int videoW = W - UI_PLAYLIST_W;
    if (videoW < 200) videoW = 200;

    HBRUSH bg = CreateSolidBrush(RGB(24, 24, 28));
    RECT rcAll = {0, 0, W, H};
    FillRect(dc, &rcAll, bg);
    DeleteObject(bg);

    /* ---- Video / audio area ---- */
    RECT rcVid = {0, 0, videoW, videoH};
    if (!IsAudioOnly() && g_frameBuf && g_frameW > 0 && g_frameH > 0) {
        int vw = g_frameW, vh = g_frameH;
        double sa = (double)videoW / vw, sb = (double)videoH / vh;
        double s  = sa < sb ? sa : sb;
        int dw = (int)(vw * s), dh = (int)(vh * s);
        int dx = (videoW - dw) / 2, dy = (videoH - dh) / 2;

        HBRUSH b0 = CreateSolidBrush(RGB(12, 12, 16));
        FillRect(dc, &rcVid, b0);
        DeleteObject(b0);

        StretchDIBits(dc, dx, dy, dw, dh, 0, 0, vw, vh,
                      g_frameBuf, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        HBRUSH b2 = CreateSolidBrush(RGB(12, 12, 16));
        FillRect(dc, &rcVid, b2);
        DeleteObject(b2);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(120, 180, 255));
        SelectObject(dc, g_fontInfo);
        DrawTextW(dc, g_status, -1, &rcVid,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    }

    /* ---- Playlist panel ---- */
    g_rcPlaylist.left   = videoW;
    g_rcPlaylist.top    = 0;
    g_rcPlaylist.right  = W;
    g_rcPlaylist.bottom = videoH;

    HBRUSH plb = CreateSolidBrush(RGB(28, 28, 34));
    FillRect(dc, &g_rcPlaylist, plb);
    DeleteObject(plb);

    /* Header */
    RECT rcHead = g_rcPlaylist;
    rcHead.bottom = rcHead.top + 28;
    HBRUSH hhb = CreateSolidBrush(RGB(40, 40, 50));
    FillRect(dc, &rcHead, hhb);
    DeleteObject(hhb);

    wchar_t hdr[64];
    _snwprintf(hdr, 64, L"Playlist (%d)", g_playlistCount);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(200, 210, 230));
    SelectObject(dc, g_fontUI);
    DrawTextW(dc, hdr, -1, &rcHead, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Items */
    int itemTop = 28;
    int itemsPerPage = (g_rcPlaylist.bottom - g_rcPlaylist.top - itemTop) / UI_ITEM_H;
    if (itemsPerPage < 1) itemsPerPage = 1;

    for (int row = 0; row < itemsPerPage; ++row) {
        int idx = g_playlistScroll + row;
        if (idx >= g_playlistCount) break;

        RECT rcItem = {
            g_rcPlaylist.left,
            g_rcPlaylist.top + itemTop + row * UI_ITEM_H,
            g_rcPlaylist.right,
            g_rcPlaylist.top + itemTop + (row + 1) * UI_ITEM_H
        };

        COLORREF bgc = RGB(28, 28, 34);
        if (idx == g_playlistCur)      bgc = RGB(40, 90, 160);
        else if (idx == g_playlistHot) bgc = RGB(50, 50, 62);

        HBRUSH ib = CreateSolidBrush(bgc);
        FillRect(dc, &rcItem, ib);
        DeleteObject(ib);

        const wchar_t* name = wcsrchr(g_playlist[idx], L'\\');
        name = name ? name + 1 : g_playlist[idx];

        RECT rcText = rcItem;
        rcText.left += 8;
        rcText.right -= 8;

        SetTextColor(dc, (idx == g_playlistCur) ? RGB(255, 255, 255)
                                                 : RGB(200, 205, 215));
        DrawTextW(dc, name, -1, &rcText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    /* ---- Control panel ---- */
    int panelY = videoH;
    RECT rcPanel = {0, panelY, W, H};
    HBRUSH pb = CreateSolidBrush(RGB(36, 36, 44));
    FillRect(dc, &rcPanel, pb);
    DeleteObject(pb);

    int pad = 16;
    g_rcProgress.left   = pad;
    g_rcProgress.right  = W - pad;
    g_rcProgress.top    = panelY + 12;
    g_rcProgress.bottom = panelY + 24;

    RECT rcProgBg = g_rcProgress;
    HBRUSH pbg = CreateSolidBrush(RGB(60, 60, 72));
    FillRect(dc, &rcProgBg, pbg);
    DeleteObject(pbg);

    double cur = g_p.audioClock;
    double tot = GetTotalSec();
    if (tot <= 0) tot = 1;
    double frac;
    if (g_dragPreview >= 0) frac = g_dragPreview;
    else                    frac = cur / tot;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    RECT rcProgFill = g_rcProgress;
    rcProgFill.right = g_rcProgress.left +
        (LONG)((g_rcProgress.right - g_rcProgress.left) * frac);
    HBRUSH pfg = CreateSolidBrush(RGB(80, 170, 255));
    FillRect(dc, &rcProgFill, pfg);
    DeleteObject(pfg);

    int cx = rcProgFill.right;
    int cy = (g_rcProgress.top + g_rcProgress.bottom) / 2;
    HBRUSH dot  = CreateSolidBrush(RGB(220, 240, 255));
    HBRUSH oldB = (HBRUSH)SelectObject(dc, dot);
    HPEN   oldP = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - 6, cy - 6, cx + 6, cy + 6);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(dot);

    wchar_t tbuf[64], tcur[32], ttot[32];
    double shownSec = (g_dragPreview >= 0) ? g_dragPreview * tot : cur;
    FmtTime(shownSec, tcur, 32);
    FmtTime(tot, ttot, 32);
    _snwprintf(tbuf, 64, L"%s / %s", tcur, ttot);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(180, 200, 220));
    SelectObject(dc, g_fontUI);
    RECT rcTime = {pad, panelY + 28, pad + 220, panelY + 48};
    DrawTextW(dc, tbuf, -1, &rcTime, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    wchar_t vbuf[32];
    _snwprintf(vbuf, 32, L"Vol: %d%%", (int)(g_volume * 100 + 0.5f));
    SetTextColor(dc, RGB(180, 200, 220));
    RECT rcVol = {W - pad - 160, panelY + 28, W - pad, panelY + 48};
    DrawTextW(dc, vbuf, -1, &rcVol, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    int btnY  = panelY + 54;
    int btnH  = 30;
    int gap   = 8;
    int bw    = 76;
    int totalW = BTN_COUNT * bw + (BTN_COUNT - 1) * gap;
    int x = (W - totalW) / 2;
    if (x < pad) x = pad;

    static const wchar_t* labels[BTN_COUNT] = {
        L"Open", L"Prev", L"Play", L"Stop", L"Next", L"<<5s", L"5s>>"
    };
    for (int i = 0; i < BTN_COUNT; i++) {
        g_btns[i].rc.left   = x;
        g_btns[i].rc.top    = btnY;
        g_btns[i].rc.right  = x + bw;
        g_btns[i].rc.bottom = btnY + btnH;
        g_btns[i].id        = (BtnId)i;
        g_btns[i].text      = labels[i];
        x += bw + gap;

        COLORREF face = RGB(58, 58, 72);
        if (g_btns[i].down)     face = RGB(40, 110, 200);
        else if (g_btns[i].hot) face = RGB(80, 80, 100);

        HBRUSH b = CreateSolidBrush(face);
        FillRect(dc, &g_btns[i].rc, b);
        DeleteObject(b);

        SetTextColor(dc, RGB(230, 235, 245));
        SelectObject(dc, g_fontUI);
        DrawTextW(dc, g_btns[i].text, -1, &g_btns[i].rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_title[0]) {
        RECT rcTitle = {12, 8, videoW - 12, 34};
        SetTextColor(dc, RGB(230, 240, 255));
        SelectObject(dc, g_fontInfo);
        DrawTextW(dc, g_title, -1, &rcTitle,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

/* ====================================================================== */
/* Control actions                                                        */
/* ====================================================================== */

static void TogglePlay(void) {
    /* EOF branch: restart playback from the beginning without
     * re-opening the file.  The audio thread has already exited,
     * so it must be re-created. */
    if (g_eof && g_source && g_p.audioData) {
        SeekTo(0.0);
        g_eof = 0;
        g_p.paused = 0;

        g_audioThreadRunning = 1;
        g_hAudioThread = CreateThread(NULL, 0, AudioThreadProc, NULL, 0, NULL);
        IXAudio2SourceVoice_Start(g_source, 0, XAUDIO2_COMMIT_NOW);

        Log("TogglePlay: restart after EOF");
        return;
    }

    if (!g_playing) return;

    g_p.paused = !g_p.paused;
    if (g_source) {
        if (g_p.paused) IXAudio2SourceVoice_Stop(g_source, 0, XAUDIO2_COMMIT_NOW);
        else            IXAudio2SourceVoice_Start(g_source, 0, XAUDIO2_COMMIT_NOW);
    }
    Log("TogglePlay: paused=%d", g_p.paused);
}

static void SeekRelative(double d) {
    if (!g_playing) return;
    SeekTo(g_p.audioClock + d);
}

static void StopAndReset(void) {
    if (!g_playing) return;
    g_p.paused = 1;
    if (g_source) {
        IXAudio2SourceVoice_Stop(g_source, 0, XAUDIO2_COMMIT_NOW);
        IXAudio2SourceVoice_FlushSourceBuffers(g_source);
    }
    SeekTo(0.0);
    g_p.audioClock = 0.0;
    g_p.paused = 0;
    g_eof = 0;
    Log("StopAndReset");
}

/* ====================================================================== */
/* Window procedure                                                       */
/* ====================================================================== */

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        DragAcceptFiles(h, TRUE);
        if (!g_fontUI)   g_fontUI   = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        if (!g_fontInfo) g_fontInfo = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        SetTimer(h, 1, 50, NULL);
        Log("WM_CREATE");
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        DrawUI(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;

    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        if (PtInRectX(p, g_rcProgress)) {
            g_dragging = 1;
            SetCapture(h);
            g_dragPreview = (double)(p.x - g_rcProgress.left) /
                            (g_rcProgress.right - g_rcProgress.left);
            if (g_dragPreview < 0) g_dragPreview = 0;
            if (g_dragPreview > 1) g_dragPreview = 1;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }

        if (PtInRectX(p, g_rcPlaylist)) {
            int relY = p.y - (g_rcPlaylist.top + 28);
            if (relY >= 0) {
                int idx = g_playlistScroll + relY / UI_ITEM_H;
                if (idx >= 0 && idx < g_playlistCount) {
                    PlaylistPlay(idx);
                    InvalidateRect(h, NULL, FALSE);
                }
            }
            return 0;
        }

        for (int i = 0; i < BTN_COUNT; i++) {
            if (PtInRectX(p, g_btns[i].rc)) {
                g_btns[i].down = 1;
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        if (g_dragging) {
            double frac = (double)(p.x - g_rcProgress.left) /
                          (g_rcProgress.right - g_rcProgress.left);
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            g_dragPreview = frac;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }

        int changed = 0;

        for (int i = 0; i < BTN_COUNT; i++) {
            int hot = PtInRectX(p, g_btns[i].rc);
            if (hot != g_btns[i].hot) { g_btns[i].hot = hot; changed = 1; }
        }

        int newHot = -1;
        if (PtInRectX(p, g_rcPlaylist)) {
            int relY = p.y - (g_rcPlaylist.top + 28);
            if (relY >= 0) {
                int idx = g_playlistScroll + relY / UI_ITEM_H;
                if (idx >= 0 && idx < g_playlistCount) newHot = idx;
            }
        }
        if (newHot != g_playlistHot) { g_playlistHot = newHot; changed = 1; }

        if (changed) InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_dragging) {
            if (g_dragPreview >= 0) {
                double tot = GetTotalSec();
                if (tot > 0) SeekTo(g_dragPreview * tot);
                g_dragPreview = -1.0;
            }
            g_dragging = 0;
            ReleaseCapture();
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }

        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        for (int i = 0; i < BTN_COUNT; i++) {
            if (g_btns[i].down && PtInRectX(p, g_btns[i].rc)) {
                g_btns[i].down = 0;
                InvalidateRect(h, NULL, FALSE);
                switch (g_btns[i].id) {
                case BTN_OPEN: SendMessageW(h, WM_COMMAND, 100, 0); break;
                case BTN_PLAY: TogglePlay();                        break;
                case BTN_STOP: StopAndReset();                      break;
                case BTN_BACK: SeekRelative(-5.0);                  break;
                case BTN_FWD:  SeekRelative( 5.0);                  break;
                case BTN_PREV: PlaylistPrev();                      break;
                case BTN_NEXT: PlaylistNext();                      break;
                }
                return 0;
            }
            g_btns[i].down = 0;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);

        if (PtInRectX(p, g_rcPlaylist)) {
            g_playlistScroll -= d;
            int itemsPerPage = (g_rcPlaylist.bottom - g_rcPlaylist.top - 28) / UI_ITEM_H;
            int maxScroll = g_playlistCount - itemsPerPage;
            if (maxScroll < 0) maxScroll = 0;
            if (g_playlistScroll < 0) g_playlistScroll = 0;
            if (g_playlistScroll > maxScroll) g_playlistScroll = maxScroll;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }

        g_volume += d * 0.05f;
        if (g_volume < 0) g_volume = 0;
        if (g_volume > 1) g_volume = 1;
        ApplyVolume();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
        int firstNew = g_playlistCount;

        for (UINT i = 0; i < n; ++i) {
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(hd, i, path, MAX_PATH)) {
                PlaylistAdd(path);
            }
        }
        DragFinish(hd);

        if (firstNew < g_playlistCount) {
            PlaylistPlay(firstNew);
            PlaylistEnsureVisible(firstNew);
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_APP + 1: {
        wchar_t* path = (wchar_t*)lp;
        if (path) {
            int idx = g_playlistCount;
            PlaylistAdd(path);
            free(path);
            if (idx < g_playlistCount) {
                PlaylistPlay(idx);
                PlaylistEnsureVisible(idx);
            }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }

    case WM_APP + 2: {
        Log("WM_APP+2: playback finished");
        if (g_playing && !g_isVideo) {
            g_audioThreadRunning = 0;
            if (g_hAudioThread) {
                WaitForSingleObject(g_hAudioThread, 1000);
                CloseHandle(g_hAudioThread);
                g_hAudioThread = NULL;
            }

            /* IMPORTANT: do NOT call AudioShutdown() and do NOT free
             * g_p.audioData.  Keeping the XAudio2 source voice and the
             * decoded PCM buffer alive allows pressing Play to restart
             * the same file from the beginning (see TogglePlay). */

            g_eof = 1;
            OnPlaybackEnded();

            if (g_playlistCount > 0 && g_playlistCur >= 0) {
                int nxt = g_playlistCur + 1;
                if (nxt < g_playlistCount) {
                    Log("Auto-advance to playlist[%d]", nxt);
                    PlaylistPlay(nxt);
                    PlaylistEnsureVisible(nxt);
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) == 100) {
            wchar_t path[4096] = {0};
            OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize  = sizeof(ofn);
            ofn.hwndOwner    = h;
            ofn.lpstrFilter  = L"Media Files\0*.mp3;*.mp4;*.wav;*.msm;*.mkv;*.avi\0"
                               L"Audio\0*.mp3;*.wav;*.msm\0"
                               L"Video\0*.mp4;*.mkv;*.avi\0"
                               L"All Files\0*.*\0";
            ofn.lpstrFile    = path;
            ofn.nMaxFile     = 4096;
            ofn.lpstrInitialDir = (g_lastDir[0] ? g_lastDir : NULL);
            ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                               OFN_ALLOWMULTISELECT | OFN_EXPLORER;

            if (GetOpenFileNameW(&ofn)) {
                const wchar_t* dir  = path;
                const wchar_t* file = path + wcslen(dir) + 1;

                if (*file == 0) {
                    wchar_t full[MAX_PATH];
                    wcsncpy(full, dir, MAX_PATH); full[MAX_PATH - 1] = 0;

                    const wchar_t* sl = wcsrchr(full, L'\\');
                    if (sl) {
                        size_t n2 = sl - full;
                        if (n2 < MAX_PATH) { wcsncpy(g_lastDir, full, n2); g_lastDir[n2] = 0; }
                    }

                    int idx = g_playlistCount;
                    PlaylistAdd(full);
                    if (idx < g_playlistCount) {
                        PlaylistPlay(idx);
                        PlaylistEnsureVisible(idx);
                    }
                } else {
                    if (wcslen(dir) < MAX_PATH) {
                        wcsncpy(g_lastDir, dir, MAX_PATH);
                        g_lastDir[MAX_PATH - 1] = 0;
                    }
                    int firstNew = g_playlistCount;
                    while (*file) {
                        wchar_t full[MAX_PATH];
                        _snwprintf(full, MAX_PATH, L"%s\\%s", dir, file);
                        PlaylistAdd(full);
                        file += wcslen(file) + 1;
                    }
                    if (firstNew < g_playlistCount) {
                        PlaylistPlay(firstNew);
                        PlaylistEnsureVisible(firstNew);
                    }
                }
                InvalidateRect(h, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_SPACE) { TogglePlay(); return 0; }
        if (wp == VK_ESCAPE) { g_p.quit = 1; PostQuitMessage(0); return 0; }
        if (wp == VK_LEFT)  { SeekRelative(-5.0); return 0; }
        if (wp == VK_RIGHT) { SeekRelative( 5.0); return 0; }
        if (wp == VK_UP)    { g_volume += 0.05f; if (g_volume > 1) g_volume = 1; ApplyVolume(); return 0; }
        if (wp == VK_DOWN)  { g_volume -= 0.05f; if (g_volume < 0) g_volume = 0; ApplyVolume(); return 0; }
        if (wp == 'N')      { PlaylistNext(); return 0; }
        if (wp == 'P')      { PlaylistPrev(); return 0; }
        return 0;

    case WM_DESTROY:
        Log("WM_DESTROY");
        KillTimer(h, 1);
        SettingsSave();
        if (g_fontUI)   { DeleteObject(g_fontUI);   g_fontUI   = NULL; }
        if (g_fontInfo) { DeleteObject(g_fontInfo); g_fontInfo = NULL; }
        g_p.quit = 1;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int CreateMainWindow(const wchar_t* title, int w, int h) {
    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"SMPClass";
    RegisterClassExW(&wc);

    int x = g_winX;
    int y = g_winY;
    int ww = g_winW;
    int hh = g_winH;
    if (ww < 640) ww = 640;
    if (hh < 480) hh = 480;

    RECT rc = {0, 0, ww, hh};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, L"SMPClass", title, WS_OVERLAPPEDWINDOW,
        x, y, rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_hwnd) return 0;
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    return 1;
}

/* ====================================================================== */
/* Worker threads                                                         */
/* ====================================================================== */

static DWORD WINAPI AudioThreadProc(LPVOID param) {
    (void)param;

    Log("AudioThread start: aReader=%p", (void*)g_p.aReader);

    if (g_p.aReader) {
        while (g_audioThreadRunning && !g_p.quit) {
            if (g_p.paused) { Sleep(10); continue; }
            DWORD flags = 0; LONGLONG ts = 0; IMFSample* as = NULL;
            HRESULT hr = IMFSourceReader_ReadSample(g_p.aReader,
                MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, &ts, &as);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                Log("AudioThread(MP4): EOS hr=0x%08X flags=0x%X", (unsigned)hr, flags);
                break;
            }
            if (!as) continue;
            IMFMediaBuffer* ab = NULL;
            IMFSample_ConvertToContiguousBuffer(as, &ab);
            BYTE* pcm = NULL; DWORD maxL = 0, curL = 0;
            IMFMediaBuffer_Lock(ab, &pcm, &maxL, &curL);
            if (curL > 0) {
                AudioSubmitCopy(pcm, curL);
                g_p.audioClock += (double)curL /
                                  (g_p.aSampleRate * g_p.aChannels * 2);
            }
            IMFMediaBuffer_Unlock(ab);
            IMFMediaBuffer_Release(ab);
            IMFSample_Release(as);
        }
        Log("AudioThread(MP4) exit");
        return 0;
    }

    const size_t chunk =
        (size_t)(g_p.aSampleRate * g_p.aChannels * 2 * 0.05); /* 50 ms */

    while (g_audioThreadRunning && !g_p.quit) {
        if (g_p.paused) { Sleep(10); continue; }

        if (g_p.audioPos >= g_p.audioSize) {
            if (g_source) {
                XAUDIO2_VOICE_STATE st;
                IXAudio2SourceVoice_GetState(g_source, &st, 0);
                if (st.BuffersQueued == 0) {
                    g_audioThreadRunning = 0;
                    if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                    break;
                }
            } else {
                g_audioThreadRunning = 0;
                if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                break;
            }
            Sleep(10);
            continue;
        }

        size_t n = g_p.audioSize - g_p.audioPos;
        if (n > chunk) n = chunk;
        AudioSubmitCopy(g_p.audioData + g_p.audioPos, n);
        g_p.audioPos += n;
        g_p.audioClock = (double)g_p.audioPos /
                         (g_p.aSampleRate * g_p.aChannels * 2);
    }
    Log("AudioThread(PCM) exit");
    return 0;
}

static DWORD WINAPI VideoThreadProc(LPVOID param) {
    (void)param;
    int frameCount = 0;
    double startPts = -1;
    double baseTime = 0;
    QueryPerformanceFrequency(&g_p.qpcFreq);
    QueryPerformanceCounter(&g_p.qpcStart);

    Log("VideoThread start: %dx%d", g_p.vWidth, g_p.vHeight);

    while (g_videoThreadRunning && !g_p.quit) {
        if (g_p.paused) { Sleep(10); continue; }

        DWORD flags = 0; LONGLONG ts = 0; IMFSample* sample = NULL;
        HRESULT hr = IMFSourceReader_ReadSample(g_p.vReader,
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            Log("VideoThread: EOS hr=0x%08X flags=0x%X", (unsigned)hr, flags);
            break;
        }
        if (!sample) continue;

        IMFMediaBuffer* buf = NULL;
        IMFSample_ConvertToContiguousBuffer(sample, &buf);
        BYTE* raw = NULL; DWORD maxL = 0, curL = 0;
        IMFMediaBuffer_Lock(buf, &raw, &maxL, &curL);

        double pts = (double)ts / 10000000.0;
        if (startPts < 0) { startPts = pts; baseTime = g_p.audioClock; }
        double relPts = pts - startPts;

        int stride = g_p.vWidth * 4;
        for (int y = 0; y < g_p.vHeight; ++y)
            memcpy(g_frameBuf + y * stride, raw + y * stride, stride);

        IMFMediaBuffer_Unlock(buf);
        IMFMediaBuffer_Release(buf);
        IMFSample_Release(sample);

        if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);

        double now  = g_p.audioClock - baseTime;
        double wait = relPts - now;
        if (wait > 0.002) Sleep((DWORD)(wait * 1000));
        else if (wait < -0.1) continue;

        ++frameCount;
        if ((frameCount % 100) == 0) {
            Log("Video frame %d  pts=%.3f  audio=%.3f", frameCount, relPts, now);
        }
    }
    Log("VideoThread exit: %d frames", frameCount);
    return 0;
}

/* ====================================================================== */
/* Media openers                                                          */
/* ====================================================================== */

static int OpenMp4(const wchar_t* path) {
    IMFAttributes* attr = NULL;
    MFCreateAttributes(&attr, 1);
    IMFAttributes_SetUINT32(attr, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    HRESULT hr = MFCreateSourceReaderFromURL(path, attr, &g_p.vReader);
    if (FAILED(hr)) {
        IMFAttributes_Release(attr);
        Log("MP4 video open failed: hr=0x%08X", (unsigned)hr);
        return 0;
    }

    IMFMediaType* mt = NULL;
    MFCreateMediaType(&mt);
    IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    hr = IMFSourceReader_SetCurrentMediaType(g_p.vReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mt);
    IMFMediaType_Release(mt);
    if (FAILED(hr)) {
        IMFAttributes_Release(attr);
        Log("MP4 Set RGB32 failed: hr=0x%08X", (unsigned)hr);
        return 0;
    }

    IMFMediaType* cur = NULL;
    IMFSourceReader_GetCurrentMediaType(g_p.vReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
    UINT64 fs = 0;
    IMFMediaType_GetUINT64(cur, &MF_MT_FRAME_SIZE, &fs);
    IMFMediaType_Release(cur);
    g_p.vWidth  = (UINT32)(fs >> 32);
    g_p.vHeight = (UINT32)(fs & 0xFFFFFFFF);

    hr = MFCreateSourceReaderFromURL(path, attr, &g_p.aReader);
    IMFAttributes_Release(attr);
    if (SUCCEEDED(hr)) {
        IMFMediaType* amt = NULL;
        MFCreateMediaType(&amt);
        IMFMediaType_SetGUID(amt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        IMFMediaType_SetGUID(amt, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        hr = IMFSourceReader_SetCurrentMediaType(g_p.aReader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, amt);
        IMFMediaType_Release(amt);
        if (SUCCEEDED(hr)) {
            IMFMediaType* acur = NULL;
            IMFSourceReader_GetCurrentMediaType(g_p.aReader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &acur);
            UINT32 rate = 0, ch = 0;
            IMFMediaType_GetUINT32(acur, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            IMFMediaType_GetUINT32(acur, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
            IMFMediaType_Release(acur);
            g_p.aSampleRate = rate;
            g_p.aChannels   = ch;
        } else {
            IMFSourceReader_Release(g_p.aReader);
            g_p.aReader = NULL;
        }
    }
    g_p.type = MEDIA_MP4;
    Log("MP4: %dx%d, audio %dHz %dch", g_p.vWidth, g_p.vHeight, g_p.aSampleRate, g_p.aChannels);
    return 1;
}

static int OpenMp3(const wchar_t* path) {
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) { Log("MP3: CreateFile failed"); return 0; }

    DWORD size = GetFileSize(hf, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        Log("MP3: bad file size");
        CloseHandle(hf);
        return 0;
    }
    /* Guard against (size * 4) overflow on 32-bit hosts */
    if ((uint64_t)size * 4 > (uint64_t)SIZE_MAX) {
        Log("MP3: file too large");
        CloseHandle(hf);
        return 0;
    }

    uint8_t* buf = (uint8_t*)malloc(size);
    if (!buf) {
        Log("MP3: malloc failed");
        CloseHandle(hf);
        return 0;
    }

    DWORD rd = 0;
    ReadFile(hf, buf, size, &rd, NULL);
    CloseHandle(hf);

    mp3dec_t dec; mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples = mp3dec_decode_frame(&dec, buf, size, pcm, &info);
    if (samples <= 0) { free(buf); Log("MP3: probe failed"); return 0; }

    g_p.aSampleRate = info.hz;
    g_p.aChannels   = info.channels;

    size_t cap = (size_t)size * 4;
    int16_t* out = (int16_t*)malloc(cap);
    size_t outPos = 0, pos = 0;
    while (pos < size) {
        samples = mp3dec_decode_frame(&dec, buf + pos, (int)size - pos, pcm, &info);
        if (info.frame_bytes <= 0) break;
        pos += info.frame_bytes;
        if (samples > 0) {
            size_t bytes = (size_t)samples * info.channels * 2;
            if (outPos + bytes > cap) { cap *= 2; out = (int16_t*)realloc(out, cap); }
            memcpy((uint8_t*)out + outPos, pcm, bytes);
            outPos += bytes;
        }
    }
    free(buf);
    g_p.audioData = (uint8_t*)out;
    g_p.audioSize = outPos;
    g_p.type      = MEDIA_MP3;
    Log("MP3: %dHz %dch, %.2f sec", g_p.aSampleRate, g_p.aChannels,
        (double)outPos / (g_p.aSampleRate * g_p.aChannels * 2));
    return 1;
}

static int OpenWav(const wchar_t* path) {
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) { Log("WAV: CreateFile failed"); return 0; }
    DWORD size = GetFileSize(hf, NULL);
    uint8_t* buf = (uint8_t*)malloc(size);
    DWORD rd = 0;
    ReadFile(hf, buf, size, &rd, NULL);
    CloseHandle(hf);

    WavInfo info; memset(&info, 0, sizeof(info));
    if (!ParseWav(buf, size, &info)) { free(buf); Log("WAV: parse failed"); return 0; }
    if (info.audioFormat != 1 || info.bitsPerSample != 16) {
        free(buf);
        Log("WAV: only 16-bit PCM supported (fmt=%d bits=%d)",
            info.audioFormat, info.bitsPerSample);
        return 0;
    }
    g_p.aSampleRate = info.sampleRate;
    g_p.aChannels   = info.channels;
    g_p.audioData   = (uint8_t*)malloc(info.dataSize);
    memcpy(g_p.audioData, info.data, info.dataSize);
    g_p.audioSize   = info.dataSize;
    free(buf);
    g_p.type = MEDIA_WAV;
    Log("WAV: %dHz %dch, %.2f sec", g_p.aSampleRate, g_p.aChannels,
        (double)info.dataSize / (info.sampleRate * info.channels * 2));
    return 1;
}

static int OpenMsm(const wchar_t* path) {
    msm_decoder_t dec;
    memset(&dec, 0, sizeof(dec));

    int ret = msm_open_w(&dec, path);
    if (ret != 0) {
        Log("MSM open failed: %d", ret);
        return 0;
    }

    int16_t* pcm = NULL;
    size_t samples = 0;
    ret = msm_decode(&dec, &pcm, &samples);
    if (ret != 0) {
        Log("MSM decode failed: %d", ret);
        msm_close(&dec);
        return 0;
    }

    g_p.aSampleRate = dec.header.sample_rate;
    g_p.aChannels   = dec.header.channels;
    g_p.audioData   = (uint8_t*)pcm;
    g_p.audioSize   = samples * sizeof(int16_t);
    g_p.audioPos    = 0;
    g_p.type        = MEDIA_MSM;

    msm_close(&dec);

    Log("MSM: %dHz %dch, %.2f sec", g_p.aSampleRate, g_p.aChannels,
        (double)samples / (g_p.aSampleRate * g_p.aChannels));
    return 1;
}

static int OpenMedia(const wchar_t* path) {
    memset(&g_p, 0, sizeof(g_p));
    wcscpy(g_p.path, path);

    const wchar_t* ext = wcsrchr(path, L'.');
    if (!ext) {
        Log("No file extension");
        return 0;
    }
    if (!_wcsicmp(ext, L".mp4") || !_wcsicmp(ext, L".mkv") || !_wcsicmp(ext, L".avi"))
        return OpenMp4(path);
    if (!_wcsicmp(ext, L".mp3"))
        return OpenMp3(path);
    if (!_wcsicmp(ext, L".wav"))
        return OpenWav(path);
    if (!_wcsicmp(ext, L".msm"))
        return OpenMsm(path);

    Log("Unsupported format: %ls", ext);
    return 0;
}

/* ====================================================================== */
/* Playback state machine                                                 */
/* ====================================================================== */

static void OnPlaybackEnded(void) {
    Log("OnPlaybackEnded");
    g_status[0] = 0;
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void StartPlayback(void) {
    if (g_playing) return;
    g_p.paused = 0;
    g_eof = 0;

    if (g_p.type == MEDIA_MP4) {
        int hasAudio = (g_p.aReader != NULL);
        if (hasAudio && !AudioInit(g_p.aSampleRate, g_p.aChannels)) hasAudio = 0;
        if (g_source) {
            IXAudio2SourceVoice_Start(g_source, 0, XAUDIO2_COMMIT_NOW);
            ApplyVolume();
        }

        g_frameW = g_p.vWidth;
        g_frameH = g_p.vHeight;
        g_frameBuf = (uint8_t*)malloc(g_frameW * g_frameH * 4);
        memset(&g_bmi, 0, sizeof(g_bmi));
        g_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        g_bmi.bmiHeader.biWidth       = g_frameW;
        g_bmi.bmiHeader.biHeight      = -g_frameH;
        g_bmi.bmiHeader.biPlanes      = 1;
        g_bmi.bmiHeader.biBitCount    = 32;
        g_bmi.bmiHeader.biCompression = BI_RGB;

        if (hasAudio) {
            g_audioThreadRunning = 1;
            g_hAudioThread = CreateThread(NULL, 0, AudioThreadProc, NULL, 0, NULL);
        }
        g_videoThreadRunning = 1;
        g_hVideoThread = CreateThread(NULL, 0, VideoThreadProc, NULL, 0, NULL);
        g_isVideo = 1;
        Log("StartPlayback(MP4) audioThread=%d", hasAudio);
    } else {
        if (!AudioInit(g_p.aSampleRate, g_p.aChannels)) return;
        IXAudio2SourceVoice_Start(g_source, 0, XAUDIO2_COMMIT_NOW);
        ApplyVolume();

        g_audioThreadRunning = 1;
        g_hAudioThread = CreateThread(NULL, 0, AudioThreadProc, NULL, 0, NULL);
        g_isVideo = 0;
        Log("StartPlayback(audio-only)");
    }
    g_playing = 1;
}

static void StopPlaybackAll(void) {
    if (!g_playing) return;
    Log("StopPlaybackAll");

    g_videoThreadRunning = 0;
    g_audioThreadRunning = 0;
    if (g_hVideoThread) {
        WaitForSingleObject(g_hVideoThread, 3000);
        CloseHandle(g_hVideoThread);
        g_hVideoThread = NULL;
    }
    if (g_hAudioThread) {
        WaitForSingleObject(g_hAudioThread, 3000);
        CloseHandle(g_hAudioThread);
        g_hAudioThread = NULL;
    }

    if (g_frameBuf) { free(g_frameBuf); g_frameBuf = NULL; }
    if (g_p.vReader) { IMFSourceReader_Release(g_p.vReader); g_p.vReader = NULL; }
    if (g_p.aReader) { IMFSourceReader_Release(g_p.aReader); g_p.aReader = NULL; }
    if (g_p.audioData) { free(g_p.audioData); g_p.audioData = NULL; }
    AudioShutdown();

    g_playing      = 0;
    g_eof          = 0;
    g_p.audioPos   = 0;
    g_p.audioClock = 0;
}

static void PlayNewFile(const wchar_t* path) {
    Log("PlayNewFile: %s", WideToUtf8(path));

    StopPlaybackAll();
    memset(&g_p, 0, sizeof(g_p));
    wcscpy(g_p.path, path);
    wcscpy(g_curPath, path);
    wcscpy(g_title, path);

    if (!OpenMedia(path)) {
        wchar_t msg[MAX_PATH + 128];
        _snwprintf(msg, MAX_PATH + 128,
                   L"Cannot open file:\n%s\n\nUnsupported or corrupted.",
                   path);
        ShowError(msg);
        wcscpy(g_status, L"Failed to open, or unsupported format");
        if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    if (g_p.type == MEDIA_MP4) {
        PROPVARIANT var; PropVariantInit(&var);
        if (g_p.vReader &&
            SUCCEEDED(IMFSourceReader_GetPresentationAttribute(
                g_p.vReader, MF_SOURCE_READER_MEDIASOURCE,
                &MF_PD_DURATION, &var)) && var.vt == VT_UI8) {
            g_totalSec = (double)var.uhVal.QuadPart / 10000000.0;
        } else {
            g_totalSec = 0;
        }
        PropVariantClear(&var);
        _snwprintf(g_status, 512, L"Video %dx%d  %dHz %dch",
                   g_p.vWidth, g_p.vHeight, g_p.aSampleRate, g_p.aChannels);
    } else {
        g_totalSec = GetTotalSec();
        _snwprintf(g_status, 512, L"Audio  %dHz %dch",
                   g_p.aSampleRate, g_p.aChannels);
    }

    Log("Duration: %.2f sec", g_totalSec);

    StartPlayback();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ====================================================================== */
/* Entry point                                                            */
/* ====================================================================== */

int wmain(int argc, wchar_t** argv) {
    LogOpen();
    Log("=== SMP started ===");

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    SettingsLoad();

    if (!CreateMainWindow(L"SMP", g_winW, g_winH)) {
        Log("CreateMainWindow failed");
        MFShutdown();
        CoUninitialize();
        LogClose();
        return 1;
    }

    if (g_master) ApplyVolume();

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            Log("Adding from cmdline: %s", WideToUtf8(argv[i]));
            PlaylistAdd(argv[i]);
        }
        if (g_playlistCount > 0) {
            PlaylistPlay(0);
            PlaylistEnsureVisible(0);
        }
    } else {
        InvalidateRect(g_hwnd, NULL, FALSE);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopPlaybackAll();
    PlaylistClear();
    SettingsSave();
    Log("=== SMP exit ===");
    MFShutdown();
    CoUninitialize();
    LogClose();
    return 0;
}