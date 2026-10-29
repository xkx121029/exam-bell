// 考试自动打铃 —— 原生 Win32 实现（无任何运行时依赖）
// 依赖仅限 Windows 自带组件：KERNEL32 / USER32 / GDI32 / WINMM / WINHTTP / msvcrt.dll
// 适用 Windows 7 SP1 x64 及以上

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <winhttp.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <wctype.h>
#include <locale.h>

// ---------------------------------------------------------------------------
// 配色
// ---------------------------------------------------------------------------
static const COLORREF C_BG       = RGB(245, 246, 248);
static const COLORREF C_SURFACE  = RGB(255, 255, 255);
static const COLORREF C_INK      = RGB(24, 27, 31);
static const COLORREF C_MUTED    = RGB(108, 116, 128);
static const COLORREF C_LINE     = RGB(224, 227, 233);
static const COLORREF C_GREEN    = RGB(22, 105, 74);
static const COLORREF C_GREEN_H  = RGB(28, 122, 88);
static const COLORREF C_GREEN_P  = RGB(18, 86, 60);
static const COLORREF C_RED      = RGB(196, 30, 34);
static const COLORREF C_RED_H    = RGB(218, 52, 56);
static const COLORREF C_RED_P    = RGB(166, 20, 24);
static const COLORREF C_OKTXT    = RGB(30, 107, 79);
static const COLORREF C_BADTXT   = RGB(179, 38, 30);
static const COLORREF C_DISABLED = RGB(185, 190, 198);

static const COLORREF C_ST_PEND_BG = RGB(237, 239, 242);
static const COLORREF C_ST_PEND_FG = RGB(91, 101, 114);
static const COLORREF C_ST_PLAY_BG = RGB(251, 227, 204);
static const COLORREF C_ST_PLAY_FG = RGB(138, 75, 0);
static const COLORREF C_ST_DONE_BG = RGB(220, 235, 221);
static const COLORREF C_ST_DONE_FG = RGB(42, 107, 60);
static const COLORREF C_ST_MISS_BG = RGB(246, 222, 222);
static const COLORREF C_ST_MISS_FG = RGB(163, 42, 38);
static const COLORREF C_ST_CANC_BG = RGB(243, 228, 228);
static const COLORREF C_ST_CANC_FG = RGB(150, 68, 68);

// ---------------------------------------------------------------------------
// 常量
// ---------------------------------------------------------------------------
enum CtlId
{
    ID_START = 1000, ID_STOP, ID_SYNC, ID_FILL, ID_EDIT_START, ID_LOG,
    ID_PICK0 = 1010, ID_PICK1, ID_PICK2,
    ID_PLAY0 = 1020, ID_PLAY1, ID_PLAY2
};

#define WM_SYNC_DONE (WM_APP + 10)
#define TIMER_TICK   1

enum NodeState { ST_NONE = 0, ST_PEND, ST_PLAY, ST_DONE, ST_MISS, ST_CANC };
enum BtnKind   { BTN_TOOL = 0, BTN_PRIMARY, BTN_DANGER };

struct BtnInfo
{
    int kind;
    COLORREF normal;
    COLORREF hover;
    COLORREF pressed;
};

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
static int  g_dpi = 96;
static HWND g_hwnd = NULL;

static HWND hTitle = NULL, hSubtitle = NULL, hClock = NULL, hDate = NULL;
static HWND hSync = NULL, hCountdown = NULL, hBtnSync = NULL;
static HWND hGrpSet = NULL, hLblSetCap = NULL, hEditStart = NULL, hBtnFill = NULL, hHint = NULL;
static HWND hGrpNode = NULL, hNodeName[3] = {0}, hNodeTime[3] = {0}, hNodeState[3] = {0};
static HWND hGrpAudio = NULL, hAudioCap[3] = {0}, hAudioFile[3] = {0}, hBtnPick[3] = {0}, hBtnPlay[3] = {0};
static HWND hGrpLog = NULL, hLog = NULL;
static HWND hBtnStart = NULL, hBtnStop = NULL;

static BtnInfo biTool, biPrimary, biDanger;

static COLORREF g_syncTextColor = C_MUTED;

static HFONT fTitle, fSub, fClock, fDate, fSync, fCount, fCap, fEdit, fRow, fState, fHint, fLog, fBtnTool, fStart, fStop;

// 时钟基准
static __int64   g_baseLocalFt = 0;
static ULONGLONG g_baseTick = 0;
static __int64   g_offsetTicks = 0;
static bool      g_synced = false;
static bool      g_syncBusy = false;
static __int64   g_lastOffsetMs = 0;
static ULONGLONG g_lastSyncTick = 0;

// 节点
static __int64      g_nodeFt[3] = {0, 0, 0};
static int          g_nodeState[3] = {ST_NONE, ST_NONE, ST_NONE};
static std::wstring g_audio[3];

// 运行状态
static bool g_running = false;
static bool g_stoppedByUser = false;
static int  g_playingIndex = -1;

// 待启动（校时完成后才真正开始）
static bool    g_pendingStart = false;
static __int64 g_pendingStartFt = 0;

// MCI
static bool g_mciOpen = false;

// 刷子
static HBRUSH bBg, bSurface, bLine, bEdit;
static HBRUSH bStatePend, bStatePlay, bStateDone, bStateMiss, bStateCanc, bDisabledBg;

// ---------------------------------------------------------------------------
// 基础工具
// ---------------------------------------------------------------------------
static int S(int v)
{
    return MulDiv(v, g_dpi, 96);
}

static HFONT MakeFont(int pt10, bool bold)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(pt10, g_dpi, 720);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(lf.lfFaceName, L"Microsoft YaHei");
    return CreateFontIndirectW(&lf);
}

static __int64 LocalFtNow()
{
    // 本机"墙上时间"：即使校时失败，也按本地时区显示，而不是 UTC
    SYSTEMTIME st;
    GetLocalTime(&st);

    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);

    LARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

// 当前北京时间（以 FILETIME 数值承载，字段即北京时间的年月日时分秒）
static __int64 FtNow()
{
    ULONGLONG elapsed = GetTickCount64() - g_baseTick;
    return g_baseLocalFt + (__int64)elapsed * 10000LL + g_offsetTicks;
}

static void RebaseClock()
{
    g_baseLocalFt = LocalFtNow();
    g_baseTick = GetTickCount64();
}

static SYSTEMTIME FtToSystemTime(__int64 ticks)
{
    FILETIME ft;
    LARGE_INTEGER li;
    li.QuadPart = ticks;
    ft.dwLowDateTime = li.LowPart;
    ft.dwHighDateTime = li.HighPart;

    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    FileTimeToSystemTime(&ft, &st);
    return st;
}

static __int64 SystemTimeToFt(const SYSTEMTIME& st)
{
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    LARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

static std::wstring FormatTime(__int64 ticks)
{
    SYSTEMTIME st = FtToSystemTime(ticks);
    wchar_t buf[64];
    swprintf(buf, 64, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring FormatDateTime(__int64 ticks)
{
    SYSTEMTIME st = FtToSystemTime(ticks);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring FormatDate(__int64 ticks)
{
    SYSTEMTIME st = FtToSystemTime(ticks);
    static const wchar_t* week[7] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };

    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d  ", st.wYear, st.wMonth, st.wDay);

    std::wstring text = buf;
    text += L"星期";
    text += week[st.wDayOfWeek % 7];
    text += L"\u3000";
    return text;
}

static std::wstring NodeName(int index)
{
    wchar_t buf[160];
    GetWindowTextW(hNodeName[index], buf, 160);
    return buf;
}

static std::wstring FileNameOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L'\\');
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

static bool FileExists(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA attr;
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr) != 0;
}

static void Log(const std::wstring& message)
{
    if (!hLog) return;
    std::wstring line = L"[" + FormatTime(FtNow()) + L"] " + message + L"\r\n";
    SendMessageW(hLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

// ---------------------------------------------------------------------------
// 配置读写（%APPDATA%\ExamBell\config.ini，UTF-8）
// ---------------------------------------------------------------------------
static std::wstring ConfigPath()
{
    wchar_t dir[MAX_PATH] = L"C:\\";
    if (GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH) == 0)
    {
        lstrcpyW(dir, L"C:\\Temp");
    }

    std::wstring folder = std::wstring(dir) + L"\\ExamBell";
    CreateDirectoryW(folder.c_str(), NULL);
    return folder + L"\\config.ini";
}

static std::wstring Trim(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) b++;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) e--;
    return s.substr(b, e - b);
}

static void SaveSettings()
{
    std::wstring text = L"# 考试自动打铃 配置文件\r\n";

    wchar_t start[64] = {0};
    GetWindowTextW(hEditStart, start, 64);
    text += L"start=" + std::wstring(start) + L"\r\n";

    for (int i = 0; i < 3; i++)
    {
        wchar_t key[16];
        swprintf(key, 16, L"audio%d=", i);
        text += key;
        text += g_audio[i];
        text += L"\r\n";
    }

    std::wstring path = ConfigPath();

    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string utf8(need > 0 ? need : 0, '\0');
    if (need > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], need, NULL, NULL);
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    CloseHandle(file);
}

static void LoadSettings()
{
    std::wstring startValue;
    std::wstring audioValue[3];
    bool hasConfig = false;

    std::wstring path = ConfigPath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file != INVALID_HANDLE_VALUE)
    {
        hasConfig = true;

        LARGE_INTEGER size;
        size.QuadPart = 0;
        GetFileSizeEx(file, &size);

        if (size.QuadPart > 0 && size.QuadPart < 1024 * 1024)
        {
            std::string raw((size_t)size.QuadPart, '\0');
            DWORD read = 0;
            ReadFile(file, &raw[0], (DWORD)raw.size(), &read, NULL);
            raw.resize(read);

            int need = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), NULL, 0);
            if (need > 0)
            {
                std::wstring text(need, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &text[0], need);

                size_t pos = 0;
                while (pos < text.size())
                {
                    size_t eol = text.find(L'\n', pos);
                    if (eol == std::wstring::npos) eol = text.size();

                    std::wstring line = Trim(text.substr(pos, eol - pos));
                    pos = eol + 1;

                    if (line.empty() || line[0] == L'#') continue;

                    size_t eq = line.find(L'=');
                    if (eq == std::wstring::npos || eq == 0) continue;

                    std::wstring key = Trim(line.substr(0, eq));
                    std::wstring value = Trim(line.substr(eq + 1));

                    if (key == L"start") startValue = value;
                    else if (key == L"audio0") audioValue[0] = value;
                    else if (key == L"audio1") audioValue[1] = value;
                    else if (key == L"audio2") audioValue[2] = value;
                }
            }
        }

        CloseHandle(file);
    }

    if (hasConfig && !startValue.empty())
    {
        SetWindowTextW(hEditStart, startValue.c_str());
    }

    for (int i = 0; i < 3; i++)
    {
        g_audio[i] = audioValue[i];
    }
}

// 从 exe 同级 audio 目录按 01 / 02 / 03 前缀自动识别音频
static std::wstring FindDefaultAudio(int index)
{
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring dir = exePath;
    size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos) dir = dir.substr(0, slash);

    std::wstring audioDir = dir + L"\\audio\\";

    wchar_t prefix[8];
    swprintf(prefix, 8, L"%02d", index + 1);

    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((audioDir + L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return L"";

    std::wstring best;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring name = data.cFileName;
        if (name.size() < 2) continue;
        if (wcsncmp(name.c_str(), prefix, 2) != 0) continue;

        std::wstring full = audioDir + name;
        size_t dot = full.find_last_of(L'.');
        if (dot == std::wstring::npos) continue;

        std::wstring ext = full.substr(dot);
        for (size_t k = 0; k < ext.size(); k++) ext[k] = towlower(ext[k]);
        if (ext != L".wav" && ext != L".mp3" && ext != L".wma") continue;

        if (best.empty() || full.compare(best) < 0) best = full;
    }
    while (FindNextFileW(find, &data));

    FindClose(find);
    return best;
}

// ---------------------------------------------------------------------------
// 音频播放（MCI）
// ---------------------------------------------------------------------------
static bool McSend(const std::wstring& cmd)
{
    return mciSendStringW(cmd.c_str(), NULL, 0, NULL) == 0;
}

static void MciClose()
{
    if (!g_mciOpen) return;
    McSend(L"stop exam_bell");
    McSend(L"close exam_bell");
    g_mciOpen = false;
}

static void MciStop()
{
    if (!g_mciOpen) return;
    McSend(L"stop exam_bell");
}

static bool MciIsPlaying()
{
    if (!g_mciOpen) return false;

    wchar_t buf[64] = {0};
    if (mciSendStringW(L"status exam_bell mode", buf, 64, NULL) != 0) return false;

    CharUpperW(buf);
    return wcsstr(buf, L"PLAYING") != NULL;
}

static bool MciPlay(const std::wstring& path)
{
    MciClose();

    if (path.empty() || !FileExists(path)) return false;

    if (!McSend(L"open \"" + path + L"\" alias exam_bell"))
    {
        if (!McSend(L"open \"" + path + L"\" type mpegvideo alias exam_bell"))
        {
            if (!McSend(L"open \"" + path + L"\" type waveaudio alias exam_bell"))
            {
                return false;
            }
        }
    }

    g_mciOpen = true;

    if (!McSend(L"play exam_bell"))
    {
        MciClose();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 网络校时（WinHTTP，后台线程）
// ---------------------------------------------------------------------------
struct SyncResult
{
    bool ok;
    __int64 offsetTicks;
    long roundTripMs;
    std::wstring message;
    std::wstring source;
};

// 校时源类型：JSON 毫秒时间戳 / JSON 北京时间字符串 / HTTP Date 响应头
enum SyncKind { SYNC_TAOBAO, SYNC_SUNING, SYNC_DATE_HEADER };

struct TimeSource
{
    const wchar_t* host;
    const wchar_t* path;
    const wchar_t* name;
    SyncKind kind;
};

// 依次尝试；任一可用即停止。淘宝接口在部分网络下 DNS 不可达，故备有苏宁与 Date 头兜底。
static const TimeSource kTimeSources[] =
{
    { L"api.m.taobao.com", L"/rest/api3.do?api=mtop.common.getTimestamp", L"淘宝", SYNC_TAOBAO     },
    { L"quan.suning.com",  L"/getSysTime.do",                             L"苏宁", SYNC_SUNING     },
    { L"www.baidu.com",    L"/",                                          L"百度", SYNC_DATE_HEADER },
};

static bool ParseTaobaoTime(const std::string& body, __int64* serverFt)
{
    size_t pos = body.find("\"t\":\"");
    if (pos == std::string::npos) return false;
    pos += 5;

    size_t end = pos;
    while (end < body.size() && body[end] >= '0' && body[end] <= '9') end++;
    if (end == pos) return false;

    __int64 stamp = _strtoi64(body.substr(pos, end - pos).c_str(), NULL, 10);
    if (stamp < 100000000000LL) stamp *= 1000;   // 秒 -> 毫秒

    // 服务器时间戳为 UTC 毫秒，换算成北京时间对应的 FILETIME 数值
    *serverFt = stamp * 10000LL + (__int64)8 * 3600 * 10000000LL;
    return true;
}

static bool ParseSuningTime(const std::string& body, __int64* serverFt)
{
    size_t pos = body.find("\"sysTime2\":\"");
    if (pos == std::string::npos) return false;
    pos += 12;   // 跳过 "sysTime2":"

    size_t end = body.find('"', pos);
    if (end == std::string::npos) return false;

    int y = 0, mo = 0, d = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(body.substr(pos, end - pos).c_str(), "%d-%d-%d %d:%d:%d",
               &y, &mo, &d, &hh, &mm, &ss) != 6)
        return false;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;

    // 接口返回的就是北京时间墙上时间，直接作为基准
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
    st.wHour = (WORD)hh; st.wMinute = (WORD)mm; st.wSecond = (WORD)ss;
    *serverFt = SystemTimeToFt(st);
    return true;
}

// 拉取单个时间源，成功后输出"服务器当前的北京时间"与本次往返时延
static bool FetchServerTime(const TimeSource& src, __int64* serverFt,
                            __int64* afterFt, long* rttMs, std::wstring* err)
{
    HINTERNET session = WinHttpOpen(L"ExamBell/1.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        *err = L"无法初始化网络组件";
        return false;
    }

    WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);

    // 部分时间源会拒绝非浏览器 UA（直接返回 403），统一带上常规 UA
    const wchar_t* ua = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                        L"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";
    WinHttpSetOption(session, WINHTTP_OPTION_USER_AGENT,
                     (LPVOID)ua, (DWORD)((wcslen(ua) + 1) * sizeof(wchar_t)));

    HINTERNET connect = WinHttpConnect(session, src.host, INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!connect)
    {
        *err = L"无法连接服务器";
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", src.path, NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request)
    {
        *err = L"无法创建请求";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    __int64 before = LocalFtNow();

    BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(request, NULL);

    bool parsed = false;
    __int64 after = before;

    if (ok)
    {
        if (src.kind == SYNC_DATE_HEADER)
        {
            // 任意站点的 Date 头都是服务器 UTC 时间，精度到秒
            SYSTEMTIME st;
            ZeroMemory(&st, sizeof(st));
            DWORD size = sizeof(st);
            if (WinHttpQueryHeaders(request,
                                    WINHTTP_QUERY_DATE | WINHTTP_QUERY_FLAG_SYSTEMTIME,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &st, &size,
                                    WINHTTP_NO_HEADER_INDEX) && st.wYear >= 1970)
            {
                *serverFt = SystemTimeToFt(st) + (__int64)8 * 3600 * 10000000LL;
                parsed = true;
            }
            after = LocalFtNow();
        }
        else
        {
            std::string body;
            for (;;)
            {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail)) break;
                if (avail == 0) break;
                if (avail > 65536) avail = 65536;

                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, &chunk[0], avail, &read)) break;
                if (read == 0) break;

                chunk.resize(read);
                body += chunk;
                if (body.size() > 262144) break;
            }
            after = LocalFtNow();

            if (src.kind == SYNC_TAOBAO) parsed = ParseTaobaoTime(body, serverFt);
            else                          parsed = ParseSuningTime(body, serverFt);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    *afterFt = after;
    *rttMs = (long)((after - before) / 10000LL);

    if (!ok)      { *err = L"连接失败（网络不可达或被拦截）"; return false; }
    if (!parsed)  { *err = L"返回内容中没有可识别的时间字段"; return false; }
    return true;
}

static SyncResult* DoSyncRequest()
{
    SyncResult* result = new SyncResult();
    result->ok = false;
    result->offsetTicks = 0;
    result->roundTripMs = 0;

    int count = (int)(sizeof(kTimeSources) / sizeof(kTimeSources[0]));
    std::wstring lastErr;

    for (int i = 0; i < count && !result->ok; i++)
    {
        __int64 serverFt = 0;
        __int64 afterFt = 0;
        long rtt = 0;
        std::wstring err;

        if (!FetchServerTime(kTimeSources[i], &serverFt, &afterFt, &rtt, &err))
        {
            lastErr = err;
            continue;
        }

        // 往返时延取中值作为基准时刻
        __int64 reference = afterFt - (__int64)rtt * 10000LL / 2;

        result->offsetTicks = serverFt - reference;
        result->roundTripMs = rtt;
        result->source = kTimeSources[i].name;
        result->ok = true;
    }

    if (!result->ok)
    {
        result->message = L"所有时间源均不可达（未联网或被防火墙拦截）";
        if (!lastErr.empty()) result->message += L"：" + lastErr;
    }

    return result;
}

static DWORD WINAPI SyncThreadProc(LPVOID)
{
    SyncResult* first = DoSyncRequest();
    if (first->ok)
    {
        PostMessageW(g_hwnd, WM_SYNC_DONE, 0, (LPARAM)first);
        return 0;
    }

    delete first;
    Sleep(600);

    SyncResult* second = DoSyncRequest();
    PostMessageW(g_hwnd, WM_SYNC_DONE, 0, (LPARAM)second);
    return 0;
}

static void StartSyncThread()
{
    if (g_syncBusy) return;

    g_syncBusy = true;
    EnableWindow(hBtnSync, FALSE);
    g_syncTextColor = C_MUTED;
    SetWindowTextW(hSync, L"正在校时…");
    InvalidateRect(hSync, NULL, TRUE);

    DWORD tid = 0;
    HANDLE thread = CreateThread(NULL, 0, SyncThreadProc, NULL, 0, &tid);
    if (thread) CloseHandle(thread);
}

// ---------------------------------------------------------------------------
// 界面构建
// ---------------------------------------------------------------------------
static HWND MakeStatic(const wchar_t* text, int x, int y, int w, int h, HFONT font, DWORD extraStyle)
{
    HWND control = CreateWindowExW(0, L"STATIC", text,
                                   WS_CHILD | WS_VISIBLE | extraStyle,
                                   S(x), S(y), S(w), S(h),
                                   g_hwnd, NULL, NULL, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
    return control;
}

static HWND MakeGroup(const wchar_t* text, int x, int y, int w, int h)
{
    HWND g = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                             S(x), S(y), S(w), S(h),
                             g_hwnd, NULL, NULL, NULL);
    SendMessageW(g, WM_SETFONT, (WPARAM)fCap, TRUE);
    return g;
}

static HWND MakeOwnedButton(const wchar_t* text, int x, int y, int w, int h,
                            BtnInfo* info, HFONT font, int id)
{
    HWND b = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             S(x), S(y), S(w), S(h),
                             g_hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);
    SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)info);
    return b;
}

static WNDPROC g_editProc = NULL;

static LRESULT CALLBACK StartEditProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && w == VK_RETURN)
    {
        PostMessageW(g_hwnd, WM_COMMAND, MAKELONG(ID_START, BN_CLICKED), 0);
        return 0;
    }
    if (msg == WM_CHAR && w == VK_RETURN) return 0;

    return CallWindowProcW(g_editProc, h, msg, w, l);
}

static void BuildUi()
{
    // 字体
    fTitle   = MakeFont(190, true);
    fSub     = MakeFont(85,  false);
    fClock   = MakeFont(200, true);
    fDate    = MakeFont(85,  false);
    fSync    = MakeFont(85,  false);
    fCount   = MakeFont(115, true);
    fCap     = MakeFont(90,  false);
    fEdit    = MakeFont(110, false);
    fRow     = MakeFont(90,  false);
    fState   = MakeFont(85,  false);
    fHint    = MakeFont(85,  false);
    fLog     = MakeFont(85,  false);
    fBtnTool = MakeFont(85,  false);
    fStart   = MakeFont(150, true);
    fStop    = MakeFont(260, true);

    // 刷子
    bBg         = CreateSolidBrush(C_BG);
    bSurface    = CreateSolidBrush(C_SURFACE);
    bLine       = CreateSolidBrush(C_LINE);
    bEdit       = CreateSolidBrush(C_SURFACE);
    bStatePend  = CreateSolidBrush(C_ST_PEND_BG);
    bStatePlay  = CreateSolidBrush(C_ST_PLAY_BG);
    bStateDone  = CreateSolidBrush(C_ST_DONE_BG);
    bStateMiss  = CreateSolidBrush(C_ST_MISS_BG);
    bStateCanc  = CreateSolidBrush(C_ST_CANC_BG);
    bDisabledBg = CreateSolidBrush(RGB(233, 236, 240));

    // 按钮样式
    biTool.kind    = BTN_TOOL;
    biTool.normal  = C_SURFACE;
    biTool.hover   = RGB(242, 244, 247);
    biTool.pressed = RGB(230, 234, 239);

    biPrimary.kind    = BTN_PRIMARY;
    biPrimary.normal  = C_GREEN;
    biPrimary.hover   = C_GREEN_H;
    biPrimary.pressed = C_GREEN_P;

    biDanger.kind    = BTN_DANGER;
    biDanger.normal  = C_RED;
    biDanger.hover   = C_RED_H;
    biDanger.pressed = C_RED_P;

    // 顶部
    hTitle    = MakeStatic(L"考试自动打铃", 24, 10, 300, 40, fTitle, SS_LEFT);
    hSubtitle = MakeStatic(L"北京时间基准 · 三段音频自动播放", 26, 52, 360, 22, fSub, SS_LEFT);
    hCountdown = MakeStatic(L"待启动", 290, 22, 270, 30, fCount, SS_RIGHT);

    hClock = MakeStatic(L"--:--:--", 560, 4, 390, 42, fClock, SS_RIGHT);
    hDate  = MakeStatic(L"北京时间", 560, 46, 390, 20, fDate, SS_RIGHT);
    hSync  = MakeStatic(L"正在校时…", 560, 68, 288, 22, fSync, SS_RIGHT);
    hBtnSync = MakeOwnedButton(L"重新校时", 862, 63, 88, 28, &biTool, fBtnTool, ID_SYNC);

    // 考试设置
    hGrpSet   = MakeGroup(L"考试设置", 20, 114, 573, 114);
    hLblSetCap = MakeStatic(L"考试开始时间（北京时间）", 34, 134, 300, 22, fCap, SS_LEFT);

    hEditStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
                                 S(34), S(158), S(216), S(28),
                                 g_hwnd, (HMENU)ID_EDIT_START, NULL, NULL);
    SendMessageW(hEditStart, WM_SETFONT, (WPARAM)fEdit, TRUE);
    SendMessageW(hEditStart, EM_SETLIMITTEXT, 32, 0);

    g_editProc = (WNDPROC)SetWindowLongPtrW(hEditStart, GWLP_WNDPROC, (LONG_PTR)StartEditProc);

    hBtnFill = MakeOwnedButton(L"填入当前 +30 分钟", 258, 158, 142, 28, &biTool, fBtnTool, ID_FILL);
    hHint = MakeStatic(L"格式 YYYY-MM-DD HH:MM:SS，例如 2026-10-30 09:00:00", 34, 190, 480, 20, fHint, SS_LEFT);

    // 打铃节点
    hGrpNode = MakeGroup(L"打铃节点", 20, 238, 573, 170);
    MakeStatic(L"节点", 34, 258, 200, 20, fState, SS_LEFT);
    MakeStatic(L"预定时间", 290, 258, 150, 20, fState, SS_LEFT);
    MakeStatic(L"状态", 468, 258, 100, 20, fState, SS_LEFT);

    const wchar_t* names[3] = {
        L"考前 10 分钟 · 考生进入考场",
        L"考前 5 分钟 · 下发试卷与答题卡",
        L"考试开始 · 考生开始作答"
    };

    for (int i = 0; i < 3; i++)
    {
        int y = 280 + i * 36;
        hNodeName[i] = MakeStatic(names[i], 34, y + 8, 252, 22, fRow, SS_LEFT);
        hNodeTime[i] = MakeStatic(L"—", 290, y + 8, 160, 22, fRow, SS_LEFT);
        hNodeState[i] = MakeStatic(L"未排期", 476, y + 4, 84, 26, fState, SS_CENTER | SS_CENTERIMAGE);
    }

    // 音频文件
    hGrpAudio = MakeGroup(L"音频文件", 20, 418, 573, 136);
    const wchar_t* caps[3] = { L"考前 10 分钟", L"考前 5 分钟", L"考试开始" };

    for (int i = 0; i < 3; i++)
    {
        int y = 434 + i * 34;
        hAudioCap[i] = MakeStatic(caps[i], 34, y + 6, 92, 22, fRow, SS_LEFT);
        hAudioFile[i] = MakeStatic(L"未选择（点击“选择”添加）", 128, y + 6, 330, 22, fState, SS_LEFT);
        hBtnPick[i] = MakeOwnedButton(L"选择", 466, y, 60, 28, &biTool, fBtnTool, ID_PICK0 + i);
        hBtnPlay[i] = MakeOwnedButton(L"试听", 530, y, 60, 28, &biTool, fBtnTool, ID_PLAY0 + i);
    }

    // 运行日志
    hGrpLog = MakeGroup(L"运行日志", 605, 114, 355, 440);
    hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                           S(619), S(136), S(327), S(406),
                           g_hwnd, (HMENU)ID_LOG, NULL, NULL);
    SendMessageW(hLog, WM_SETFONT, (WPARAM)fLog, TRUE);

    // 底部操作
    hBtnStart = MakeOwnedButton(L"启 动 监 测", 20, 572, 300, 112, &biPrimary, fStart, ID_START);
    hBtnStop  = MakeOwnedButton(L"急  停", 334, 572, 626, 112, &biDanger, fStop, ID_STOP);
}

// ---------------------------------------------------------------------------
// 状态与显示刷新
// ---------------------------------------------------------------------------
static const wchar_t* StateText(int state)
{
    switch (state)
    {
        case ST_PEND: return L"待触发";
        case ST_PLAY: return L"播放中";
        case ST_DONE: return L"已完成";
        case ST_MISS: return L"已错过";
        case ST_CANC: return L"已取消";
    }
    return L"未知";
}

static void UpdateNodeRow(int index)
{
    if (index < 0 || index > 2) return;

    if (g_nodeState[index] == ST_NONE)
    {
        SetWindowTextW(hNodeTime[index], L"—");
        SetWindowTextW(hNodeState[index], L"未排期");
    }
    else
    {
        SetWindowTextW(hNodeTime[index], FormatTime(g_nodeFt[index]).c_str());
        SetWindowTextW(hNodeState[index], StateText(g_nodeState[index]));
    }

    InvalidateRect(hNodeState[index], NULL, TRUE);
    InvalidateRect(hNodeTime[index], NULL, TRUE);
}

static void UpdateAllNodeRows()
{
    for (int i = 0; i < 3; i++) UpdateNodeRow(i);
}

static void UpdateAudioLabel(int index)
{
    if (g_audio[index].empty() || !FileExists(g_audio[index]))
    {
        if (g_audio[index].empty())
        {
            SetWindowTextW(hAudioFile[index], L"未选择（点击“选择”添加）");
        }
        else
        {
            SetWindowTextW(hAudioFile[index], (L"文件已丢失：" + FileNameOf(g_audio[index])).c_str());
        }
    }
    else
    {
        SetWindowTextW(hAudioFile[index], FileNameOf(g_audio[index]).c_str());
    }
    InvalidateRect(hAudioFile[index], NULL, TRUE);
}

static void UpdateCountdown()
{
    if (g_running)
    {
        int best = -1;
        for (int i = 0; i < 3; i++)
        {
            if (g_nodeState[i] != ST_PEND) continue;
            if (best < 0 || g_nodeFt[i] < g_nodeFt[best]) best = i;
        }

        if (best < 0)
        {
            SetWindowTextW(hCountdown, L"本轮任务已完成");
        }
        else
        {
            __int64 remain = g_nodeFt[best] - FtNow();
            if (remain < 0) remain = 0;

            __int64 totalSeconds = remain / 10000000LL;
            int hours = (int)(totalSeconds / 3600);
            int minutes = (int)((totalSeconds % 3600) / 60);
            int seconds = (int)(totalSeconds % 60);

            wchar_t buf[64];
            if (hours > 0)
            {
                swprintf(buf, 64, L"距下一铃 %02d:%02d:%02d", hours, minutes, seconds);
            }
            else
            {
                swprintf(buf, 64, L"距下一铃 %02d:%02d", minutes, seconds);
            }
            SetWindowTextW(hCountdown, buf);
        }
    }
    else if (g_stoppedByUser)
    {
        SetWindowTextW(hCountdown, L"已急停 · 后续任务已取消");
    }
    else
    {
        SetWindowTextW(hCountdown, L"待启动");
    }

    InvalidateRect(hCountdown, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// 打铃调度
// ---------------------------------------------------------------------------
static void TriggerBell(int index)
{
    g_nodeState[index] = ST_PLAY;
    UpdateNodeRow(index);
    Log(L"打铃：" + NodeName(index));

    std::wstring file = g_audio[index];

    if (!file.empty())
    {
        if (FileExists(file))
        {
            if (MciPlay(file))
            {
                g_playingIndex = index;
                return;
            }
            Log(L"音频播放失败，改用系统提示音");
        }
        else
        {
            Log(L"音频文件不存在：" + file);
        }
    }
    else
    {
        Log(L"未设置音频文件，改用系统提示音");
    }

    MessageBeep(MB_ICONEXCLAMATION);
    g_nodeState[index] = ST_DONE;
    UpdateNodeRow(index);
    g_playingIndex = -1;
}

static void CheckAllFinished()
{
    for (int i = 0; i < 3; i++)
    {
        if (g_nodeState[i] == ST_PEND || g_nodeState[i] == ST_PLAY) return;
    }

    if (!g_running) return;

    g_running = false;
    EnableWindow(hBtnStart, TRUE);
    SetWindowTextW(hBtnStart, L"启 动 监 测");
    InvalidateRect(hBtnStart, NULL, TRUE);
    EnableWindow(hEditStart, TRUE);

    Log(L"本轮任务全部结束，可安排下一场");
    UpdateCountdown();
}

static void OnTick()
{
    __int64 now = FtNow();

    // 仅在文本变化时刷新，避免无谓重绘
    static std::wstring lastClock, lastDate;
    std::wstring clockText = FormatTime(now);
    std::wstring dateText  = FormatDate(now);
    if (clockText != lastClock) { lastClock = clockText; SetWindowTextW(hClock, clockText.c_str()); }
    if (dateText  != lastDate)  { lastDate  = dateText;  SetWindowTextW(hDate,  dateText.c_str());  }

    // 播放结束检测
    if (g_playingIndex >= 0 && !MciIsPlaying())
    {
        Log(L"播放结束：" + NodeName(g_playingIndex));
        g_nodeState[g_playingIndex] = ST_DONE;
        UpdateNodeRow(g_playingIndex);
        g_playingIndex = -1;
    }

    if (!g_running)
    {
        UpdateCountdown();
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        if (g_nodeState[i] != ST_PEND) continue;
        if (now < g_nodeFt[i]) continue;

        __int64 late = now - g_nodeFt[i];
        if (late <= 60LL * 10000000LL)
        {
            TriggerBell(i);          // 60 秒容差内立即补响
        }
        else
        {
            g_nodeState[i] = ST_MISS;
            UpdateNodeRow(i);
            Log(L"已错过：" + NodeName(i) + L"（预定 " + FormatTime(g_nodeFt[i]) + L"）");
        }
    }

    UpdateCountdown();
    CheckAllFinished();
}

// ---------------------------------------------------------------------------
// 启动与急停
// ---------------------------------------------------------------------------
static bool ParseStartTime(__int64* outFt)
{
    wchar_t buf[64] = {0};
    GetWindowTextW(hEditStart, buf, 64);

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (swscanf(buf, L"%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return false;
    if (y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return false;

    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)s;

    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) return false;

    LARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    *outFt = li.QuadPart;
    return true;
}

static void StartMonitoring(__int64 startFt)
{
    g_stoppedByUser = false;
    g_playingIndex = -1;
    MciClose();

    __int64 now = FtNow();
    const __int64 minute = 60LL * 10000000LL;

    g_nodeFt[0] = startFt - 10 * minute;
    g_nodeFt[1] = startFt - 5 * minute;
    g_nodeFt[2] = startFt;

    for (int i = 0; i < 3; i++)
    {
        __int64 late = now - g_nodeFt[i];
        g_nodeState[i] = (late > 60 * 10000000LL) ? ST_MISS : ST_PEND;
        if (g_nodeState[i] == ST_MISS)
        {
            Log(L"节点已过：" + NodeName(i) + L"（" + FormatTime(g_nodeFt[i]) + L"）");
        }
    }

    g_running = true;

    EnableWindow(hBtnStart, FALSE);
    SetWindowTextW(hBtnStart, L"运 行 中");
    InvalidateRect(hBtnStart, NULL, TRUE);
    EnableWindow(hEditStart, FALSE);

    Log(L"开始监测 · 考试开始 " + FormatTime(startFt));

    UpdateAllNodeRows();
    UpdateCountdown();
    SaveSettings();
}

static void PerformEmergencyStop()
{
    if (!g_running)
    {
        Log(L"急停：当前没有正在进行的监测任务");
        return;
    }

    MciStop();
    MciClose();
    g_playingIndex = -1;

    for (int i = 0; i < 3; i++)
    {
        if (g_nodeState[i] == ST_PEND || g_nodeState[i] == ST_PLAY)
        {
            g_nodeState[i] = ST_CANC;
        }
    }

    g_running = false;
    g_stoppedByUser = true;

    EnableWindow(hBtnStart, TRUE);
    SetWindowTextW(hBtnStart, L"启 动 监 测");
    InvalidateRect(hBtnStart, NULL, TRUE);
    EnableWindow(hEditStart, TRUE);

    Log(L"【急停】已停止播放并取消后续任务");

    UpdateAllNodeRows();
    UpdateCountdown();
}

static void FillStartFromClock()
{
    __int64 ft = FtNow() + 30LL * 60 * 10000000LL;

    SYSTEMTIME st = FtToSystemTime(ft);
    st.wSecond = 0;
    st.wMilliseconds = 0;

    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SetWindowTextW(hEditStart, buf);
}

static void FillDefaultStartIfEmpty()
{
    wchar_t buf[8] = {0};
    GetWindowTextW(hEditStart, buf, 8);
    if (buf[0] == L'\0') FillStartFromClock();
}

// ---------------------------------------------------------------------------
// 音频选择与试听
// ---------------------------------------------------------------------------
static void ChooseAudio(int index)
{
    wchar_t file[MAX_PATH] = {0};

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"音频文件 (*.wav;*.mp3;*.wma)\0*.wav;*.mp3;*.wma\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择提示音频";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    g_audio[index] = file;
    UpdateAudioLabel(index);
    SaveSettings();

    static const wchar_t* caps[3] = { L"考前 10 分钟", L"考前 5 分钟", L"考试开始" };
    Log(L"已设置音频（" + std::wstring(caps[index]) + L"）：" + FileNameOf(file));
}

static void PreviewAudio(int index)
{
    if (g_audio[index].empty())
    {
        MessageBoxW(g_hwnd, L"请先为该段选择音频文件。", L"未设置音频",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!FileExists(g_audio[index]))
    {
        MessageBoxW(g_hwnd, (L"音频文件不存在：\r\n" + g_audio[index]).c_str(), L"文件缺失",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (g_playingIndex >= 0)
    {
        g_nodeState[g_playingIndex] = ST_DONE;
        UpdateNodeRow(g_playingIndex);
        g_playingIndex = -1;
    }

    if (!MciPlay(g_audio[index]))
    {
        MessageBoxW(g_hwnd,
                    L"音频播放失败。\r\n\r\n请确认文件为 WAV / MP3 / WMA 格式且可正常播放，"
                    L"以及系统音频设备工作正常。",
                    L"播放失败", MB_OK | MB_ICONWARNING);
        return;
    }

    Log(L"试听：" + FileNameOf(g_audio[index]));
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
static HBRUSH StateBrush(int state)
{
    switch (state)
    {
        case ST_PEND: return bStatePend;
        case ST_PLAY: return bStatePlay;
        case ST_DONE: return bStateDone;
        case ST_MISS: return bStateMiss;
        case ST_CANC: return bStateCanc;
    }
    return bDisabledBg;
}

static COLORREF StateBgColor(int state)
{
    switch (state)
    {
        case ST_PEND: return C_ST_PEND_BG;
        case ST_PLAY: return C_ST_PLAY_BG;
        case ST_DONE: return C_ST_DONE_BG;
        case ST_MISS: return C_ST_MISS_BG;
        case ST_CANC: return C_ST_CANC_BG;
    }
    return RGB(233, 236, 240);
}

static COLORREF StateColor(int state)
{
    switch (state)
    {
        case ST_PEND: return C_ST_PEND_FG;
        case ST_PLAY: return C_ST_PLAY_FG;
        case ST_DONE: return C_ST_DONE_FG;
        case ST_MISS: return C_ST_MISS_FG;
        case ST_CANC: return C_ST_CANC_FG;
    }
    return C_MUTED;
}

static int StateIndexOf(HWND h)
{
    for (int i = 0; i < 3; i++)
    {
        if (h == hNodeState[i]) return i;
    }
    return -1;
}

static void DrawOwnedButton(LPDRAWITEMSTRUCT d)
{
    BtnInfo* info = (BtnInfo*)GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA);
    if (!info) return;

    bool pressed  = (d->itemState & ODS_SELECTED) != 0;
    bool disabled = (d->itemState & ODS_DISABLED) != 0;

    COLORREF fill;
    if (disabled)
    {
        fill = (info->kind == BTN_TOOL) ? C_SURFACE : RGB(226, 229, 234);
    }
    else
    {
        fill = pressed ? info->pressed : info->normal;
    }

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(d->hDC, &d->rcItem, brush);
    DeleteObject(brush);

    if (info->kind == BTN_TOOL)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
        HGDIOBJ oldPen = SelectObject(d->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(d->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right, d->rcItem.bottom);
        SelectObject(d->hDC, oldBrush);
        SelectObject(d->hDC, oldPen);
        DeleteObject(pen);
    }

    wchar_t text[160] = {0};
    GetWindowTextW(d->hwndItem, text, 160);

    SetBkMode(d->hDC, TRANSPARENT);

    if (info->kind == BTN_TOOL)
    {
        SetTextColor(d->hDC, disabled ? C_DISABLED : C_INK);
    }
    else
    {
        SetTextColor(d->hDC, disabled ? RGB(128, 135, 146) : RGB(255, 255, 255));
    }

    HFONT font = (info->kind == BTN_PRIMARY) ? fStart
               : (info->kind == BTN_DANGER)  ? fStop
               : fBtnTool;

    HGDIOBJ oldFont = SelectObject(d->hDC, font);
    RECT r = d->rcItem;
    DrawTextW(d->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, oldFont);
}

// ---------------------------------------------------------------------------
// 校时结果应用
// ---------------------------------------------------------------------------
static void ApplySyncResult(SyncResult* result)
{
    if (result->ok)
    {
        RebaseClock();
        g_offsetTicks = result->offsetTicks;
        g_synced = true;
        g_lastSyncTick = GetTickCount64();
        g_lastOffsetMs = result->offsetTicks / 10000LL;

        wchar_t buf[64];
        if (g_lastOffsetMs > 0)
        {
            swprintf(buf, 64, L"已同步（%ls）· 偏差 +%lld ms", result->source.c_str(), (long long)g_lastOffsetMs);
        }
        else
        {
            swprintf(buf, 64, L"已同步（%ls）· 偏差 %lld ms", result->source.c_str(), (long long)g_lastOffsetMs);
        }
        SetWindowTextW(hSync, buf);
        g_syncTextColor = C_OKTXT;

        Log(L"校时成功（" + result->source + L"）：" + FormatTime(FtNow()) +
            L"，时延 " + std::to_wstring(result->roundTripMs) + L" ms");

        FillDefaultStartIfEmpty();
        return;
    }

    bool keptPrevious = g_synced;

    RebaseClock();
    if (!keptPrevious) g_offsetTicks = 0;

    SetWindowTextW(hSync, keptPrevious ? L"校时失败 · 沿用上次偏移" : L"未同步 · 使用本机时间");
    g_syncTextColor = C_BADTXT;
    Log(L"校时失败：" + result->message +
        (keptPrevious ? L"，继续沿用上次校正结果" : L"，已回退使用本机时间"));

    FillDefaultStartIfEmpty();
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        g_hwnd = hwnd;
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT client;
        GetClientRect(hwnd, &client);

        FillRect(dc, &client, bBg);

        RECT header = client;
        header.bottom = S(100);
        FillRect(dc, &header, bSurface);

        RECT footer = client;
        footer.top = S(560);
        FillRect(dc, &footer, bSurface);

        HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 0, S(100) - 1, NULL);
        LineTo(dc, client.right, S(100) - 1);
        MoveToEx(dc, 0, S(560), NULL);
        LineTo(dc, client.right, S(560));
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;

        int idx = StateIndexOf(control);
        if (idx >= 0)
        {
            // 状态块用不透明背景，避免每秒刷新时残留旧字形
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, StateBgColor(g_nodeState[idx]));
            SetTextColor(dc, StateColor(g_nodeState[idx]));
            return (LRESULT)StateBrush(g_nodeState[idx]);
        }

        bool onHeader = (control == hTitle || control == hSubtitle || control == hClock ||
                         control == hDate  || control == hSync     || control == hCountdown);

        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, onHeader ? C_SURFACE : C_BG);

        if (control == hSync) SetTextColor(dc, g_syncTextColor);
        else if (control == hSubtitle || control == hDate) SetTextColor(dc, C_MUTED);
        else SetTextColor(dc, C_INK);

        return (LRESULT)(onHeader ? bSurface : bBg);
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, C_INK);
        SetBkColor(dc, C_SURFACE);
        return (LRESULT)bEdit;
    }

    case WM_DRAWITEM:
        DrawOwnedButton((LPDRAWITEMSTRUCT)lParam);
        return TRUE;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == ID_START && code == BN_CLICKED)
        {
            if (g_running || g_pendingStart) return 0;

            __int64 startFt = 0;
            if (!ParseStartTime(&startFt))
            {
                MessageBoxW(hwnd, L"请输入正确的考试开始时间，格式为 YYYY-MM-DD HH:MM:SS。",
                            L"时间格式有误", MB_OK | MB_ICONWARNING);
                SetFocus(hEditStart);
                SendMessageW(hEditStart, EM_SETSEL, 0, -1);
                return 0;
            }

            g_pendingStart = true;
            g_pendingStartFt = startFt;

            EnableWindow(hBtnStart, FALSE);
            SetWindowTextW(hBtnStart, L"启动中…");
            InvalidateRect(hBtnStart, NULL, TRUE);

            // 刚校时成功过（2 分钟内）则直接启动，避免对同一时间源连发请求被限流
            if (g_synced && GetTickCount64() - g_lastSyncTick < 120000)
            {
                g_pendingStart = false;

                if (startFt <= FtNow())
                {
                    MessageBoxW(hwnd,
                                (L"考试开始时间已经过去（当前北京时间 " + FormatTime(FtNow()) +
                                 L"），请重新填写。").c_str(),
                                L"时间无效", MB_OK | MB_ICONWARNING);

                    EnableWindow(hBtnStart, TRUE);
                    SetWindowTextW(hBtnStart, L"启 动 监 测");
                    InvalidateRect(hBtnStart, NULL, TRUE);
                    return 0;
                }

                StartMonitoring(startFt);
                return 0;
            }

            StartSyncThread();
            return 0;
        }

        if (id == ID_STOP && code == BN_CLICKED)
        {
            PerformEmergencyStop();
            return 0;
        }

        if (id == ID_SYNC && code == BN_CLICKED)
        {
            StartSyncThread();
            return 0;
        }

        if (id == ID_FILL && code == BN_CLICKED)
        {
            FillStartFromClock();
            return 0;
        }

        for (int i = 0; i < 3; i++)
        {
            if (id == ID_PICK0 + i && code == BN_CLICKED)
            {
                ChooseAudio(i);
                return 0;
            }
            if (id == ID_PLAY0 + i && code == BN_CLICKED)
            {
                PreviewAudio(i);
                return 0;
            }
        }

        return 0;
    }

    case WM_SYNC_DONE:
    {
        SyncResult* result = (SyncResult*)lParam;

        g_syncBusy = false;
        EnableWindow(hBtnSync, TRUE);

        if (result)
        {
            ApplySyncResult(result);
            delete result;
        }

        InvalidateRect(hSync, NULL, TRUE);
        InvalidateRect(hCountdown, NULL, TRUE);

        if (g_pendingStart)
        {
            g_pendingStart = false;

            if (g_pendingStartFt <= FtNow())
            {
                MessageBoxW(hwnd,
                            (L"考试开始时间已经过去（当前北京时间 " + FormatTime(FtNow()) +
                             L"），请重新填写。").c_str(),
                            L"时间无效", MB_OK | MB_ICONWARNING);

                EnableWindow(hBtnStart, TRUE);
                SetWindowTextW(hBtnStart, L"启 动 监 测");
                InvalidateRect(hBtnStart, NULL, TRUE);
                return 0;
            }

            StartMonitoring(g_pendingStartFt);
        }

        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_TICK) OnTick();
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
        {
            Log(L"系统已从休眠恢复，重新校时并校正计时基准");
            StartSyncThread();
        }
        return TRUE;

    case WM_CLOSE:
        if (g_running)
        {
            if (MessageBoxW(hwnd,
                            L"打铃任务正在运行，关闭程序将中止全部打铃。\r\n确定要关闭吗？",
                            L"确认关闭",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            {
                return 0;
            }
        }

        SaveSettings();
        MciClose();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------------------------------------------------------------------
// 入口
// -----------------------------------------------------------------------------------------------------------------------------------------
static void CenterWindow(HWND hwnd)
{
    RECT area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);

    RECT self;
    GetWindowRect(hwnd, &self);

    int w = self.right - self.left;
    int h = self.bottom - self.top;

    int x = area.left + ((area.right - area.left) - w) / 2;
    int y = area.top + ((area.bottom - area.top) - h) / 2;
    if (y < area.top) y = area.top;

    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    SetProcessDPIAware();

    HDC screen = GetDC(NULL);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    if (g_dpi <= 0) g_dpi = 96;

    InitCommonControls();

    RebaseClock();

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.lpszClassName = L"ExamBellWindow";
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, S(980), S(700) };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(0, L"ExamBellWindow", L"考试自动打铃",
                                WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                NULL, NULL, instance, NULL);
    if (!hwnd) return 1;

    BuildUi();
    LoadSettings();

    for (int i = 0; i < 3; i++)
    {
        if (g_audio[i].empty()) g_audio[i] = FindDefaultAudio(i);
        UpdateAudioLabel(i);
    }
    UpdateAllNodeRows();

    ShowWindow(hwnd, SW_SHOWNORMAL);
    CenterWindow(hwnd);
    UpdateWindow(hwnd);

    SetTimer(hwnd, TIMER_TICK, 200, NULL);

    // 启动即校时
    PostMessageW(hwnd, WM_COMMAND, MAKELONG(ID_SYNC, BN_CLICKED), 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && g_running)
        {
            PerformEmergencyStop();
            continue;
        }

        if (IsDialogMessageW(hwnd, &msg)) continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
