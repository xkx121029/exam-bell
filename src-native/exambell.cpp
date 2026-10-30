// 考试自动打铃 —— 原生 Win32 实现（无任何运行时依赖）
// 依赖仅限 Windows 自带组件：KERNEL32 / USER32 / GDI32 / WINMM / WINHTTP / msvcrt.dll
// 适用 Windows 7 SP1 x64 及以上

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <wctype.h>
#include <locale.h>
#include <vector>
#include <mfapi.h>
#include <mfplay.h>

// ---------------------------------------------------------------------------
// 配色
// ---------------------------------------------------------------------------
static const COLORREF C_BG       = RGB(246, 247, 249);
static const COLORREF C_SURFACE  = RGB(255, 255, 255);
static const COLORREF C_INK      = RGB(23, 26, 31);
static const COLORREF C_MUTED    = RGB(110, 118, 130);
static const COLORREF C_FAINT    = RGB(152, 159, 169);
static const COLORREF C_LINE     = RGB(226, 229, 234);
static const COLORREF C_HAIR     = RGB(240, 242, 245);
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
    ID_START = 1000, ID_STOP, ID_SYNC, ID_FILL, ID_EDIT_NAME, ID_LOG,
    ID_EXAM_LIST, ID_ADD_EXAM, ID_DEL_EXAM, ID_CLEAR_EXAM, ID_TIME_PICK,
    ID_PICK0 = 1030, ID_PICK1, ID_PICK2,
    ID_PLAY0 = 1040, ID_PLAY1, ID_PLAY2
};

#define WM_SYNC_DONE (WM_APP + 10)
#define TIMER_TICK   1

#define MAX_EXAMS    24
#define NODE_COUNT   3

enum NodeState { ST_NONE = 0, ST_PEND, ST_PLAY, ST_DONE, ST_MISS, ST_CANC };
enum BtnKind   { BTN_TOOL = 0, BTN_PRIMARY, BTN_DANGER };

// 三个打铃节点相对考试开始时间的偏移（分钟）
static const int kNodeOffsetMin[NODE_COUNT] = { -10, -5, 0 };
static const wchar_t* kNodeName[NODE_COUNT] =
{
    L"考前 10 分钟 · 考生进入考场",
    L"考前 5 分钟 · 下发试卷与答题卡",
    L"考试开始 · 考生开始作答"
};
static const wchar_t* kNodeShort[NODE_COUNT] = { L"考前 10 分钟", L"考前 5 分钟", L"考试开始" };

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
static HWND hEditName = NULL, hTimePick = NULL, hBtnAdd = NULL, hBtnDel = NULL;
static HWND hBtnClear = NULL, hExamCount = NULL, hSetHint = NULL;
static HWND hExamList = NULL;
static HWND hNodeTitle = NULL, hNodeName[3] = {0}, hNodeTime[3] = {0}, hNodeState[3] = {0};
static HWND hAudioCap[3] = {0}, hAudioFile[3] = {0}, hBtnPick[3] = {0}, hBtnPlay[3] = {0};
static HWND hLog = NULL;
static HWND hBtnStart = NULL, hBtnStop = NULL, hFootHint = NULL;

// 卡片区域（逻辑坐标）
struct CardRect { int x, y, w, h; };

static const CardRect kCardExam  = { 20, 92, 640, 306 };    // 考试日程
static const CardRect kCardNode  = { 20, 406, 640, 230 };   // 打铃节点
static const CardRect kCardAudio = { 676, 92, 324, 196 };   // 音频文件
static const CardRect kCardLog   = { 676, 296, 324, 340 };  // 运行日志

static const int kHeaderH = 84;
static const int kFooterY = 668;

static BtnInfo biTool, biPrimary, biDanger;

static COLORREF g_syncTextColor = C_MUTED;
// 顶部运行状态：0 待启动 / 1 运行中 / 2 临近 1 分钟 / 3 已完成 / 4 已急停
static int      g_runChip = 0;

static HFONT fTitle, fSub, fClock, fDate, fSync, fCap, fEdit, fRow, fState, fHint, fLog, fBtnTool, fStart, fStop;

// 时钟基准
static __int64   g_baseLocalFt = 0;
static ULONGLONG g_baseTick = 0;
static __int64   g_offsetTicks = 0;
static bool      g_synced = false;
static bool      g_syncBusy = false;
static __int64   g_lastOffsetMs = 0;
static ULONGLONG g_lastSyncTick = 0;

// 考试场次（每场固定三个打铃节点）
struct Exam
{
    std::wstring name;
    __int64      startFt;
    __int64      nodeFt[NODE_COUNT];
    int          nodeState[NODE_COUNT];
};

static Exam         g_exam[MAX_EXAMS];
static int          g_examCount = 0;
static int          g_selectedExam = -1;
static std::wstring g_audio[3];

// 运行状态
static bool g_running = false;
static bool g_stoppedByUser = false;
static int  g_playExam = -1;
static int  g_playKind = -1;

// 待启动（校时完成后才真正开始）
static bool g_pendingStart = false;

// MCI
static bool g_mciOpen = false;

// 刷子
static HBRUSH bBg, bSurface, bLine, bEdit, bSel, bLog;
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

// 界面中展示的"短日期 + 时分"，例如 10-30 09:00
static std::wstring FormatMonthDayHm(__int64 ticks)
{
    SYSTEMTIME st = FtToSystemTime(ticks);
    wchar_t buf[64];
    swprintf(buf, 64, L"%02d-%02d %02d:%02d", st.wMonth, st.wDay, st.wHour, st.wMinute);
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
    if (index < 0 || index >= NODE_COUNT) return L"";
    return kNodeShort[index];
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

static void Log(const std::wstring& message);

// ---------------------------------------------------------------------------
// 运行日志落盘（%APPDATA%\ExamBell\exam_bell.log）
//   考试现场出事需要事后追查，UI 里的日志会被清掉，文件里的不会
// ---------------------------------------------------------------------------
static std::wstring AppDir()
{
    wchar_t dir[MAX_PATH] = L"C:\\";
    if (GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH) == 0)
    {
        lstrcpyW(dir, L"C:\\Temp");
    }

    std::wstring folder = std::wstring(dir) + L"\\ExamBell";
    CreateDirectoryW(folder.c_str(), NULL);
    return folder;
}

static std::wstring LogFilePath()
{
    return AppDir() + L"\\exam_bell.log";
}

// 日志超过 2 MB 就滚动一份 .1，避免长期运行把磁盘写满
static void RotateLogIfNeeded()
{
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(LogFilePath().c_str(), GetFileExInfoStandard, &attr)) return;

    ULARGE_INTEGER size;
    size.LowPart = attr.nFileSizeLow;
    size.HighPart = attr.nFileSizeHigh;
    if (size.QuadPart < 2u * 1024u * 1024u) return;

    std::wstring path = LogFilePath();
    MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
}

static void AppendLogFile(const std::wstring& line)
{
    RotateLogIfNeeded();

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"%04d-%02d-%02d %02d:%02d:%02d  ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring text = std::wstring(stamp) + line + L"\r\n";

    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (need <= 0) return;

    std::string utf8((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], need, NULL, NULL);

    HANDLE file = CreateFileW(LogFilePath().c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    CloseHandle(file);
}

// 未捕获异常：先把现场写进日志，再让系统走默认流程
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info)
{
    wchar_t buf[256];
    DWORD code = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionCode : 0;
    void* addr = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionAddress : NULL;

    swprintf(buf, 256, L"【崩溃】异常码 0x%08X，地址 %p", (unsigned)code, addr);
    AppendLogFile(buf);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void Log(const std::wstring& message)
{
    AppendLogFile(message);

    if (!hLog) return;
    std::wstring line = L"[" + FormatTime(FtNow()) + L"] " + message + L"\r\n";
    SendMessageW(hLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);

    // UI 日志过长会拖慢重绘，超过 500 行就清一次，文件里仍保留完整记录
    static int uiLines = 0;
    uiLines++;
    if (uiLines > 500)
    {
        uiLines = 1;
        SetWindowTextW(hLog, L"");
        std::wstring tip = L"[" + FormatTime(FtNow()) + L"] （界面日志已折叠，完整记录见 exam_bell.log）\r\n";
        SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)tip.c_str());
        SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// 配置读写（%APPDATA%\ExamBell\config.ini，UTF-8）
// ---------------------------------------------------------------------------
static std::wstring ConfigPath()
{
    return AppDir() + L"\\config.ini";
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
    text += L"# examCount 为场次数量，examN_name / examN_time 依次描述每一场\r\n";

    wchar_t count[16];
    swprintf(count, 16, L"examCount=%d\r\n", g_examCount);
    text += count;

    for (int i = 0; i < g_examCount; i++)
    {
        wchar_t key[32];
        swprintf(key, 32, L"exam%d_name=", i);
        text += key;
        text += g_exam[i].name;
        text += L"\r\n";

        swprintf(key, 32, L"exam%d_time=", i);
        SYSTEMTIME st = FtToSystemTime(g_exam[i].startFt);
        wchar_t buf[64];
        swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        text += key;
        text += buf;
        text += L"\r\n";
    }

    for (int i = 0; i < 3; i++)
    {
        wchar_t key[16];
        swprintf(key, 16, L"audio%d=", i);
        text += key;
        text += g_audio[i];
        text += L"\r\n";
    }

    std::wstring path = ConfigPath();

    // 写入 UTF-8 BOM，保证 Win7 记事本直接打开也不会乱码
    std::string utf8("\xEF\xBB\xBF", 3);
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (need > 0)
    {
        size_t base = utf8.size();
        utf8.resize(base + need);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[base], need, NULL, NULL);
    }

    // 先写临时文件再原子替换，避免掉电 / 崩溃把原配置截断成半截
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    BOOL ok = WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    ok = ok && (written == (DWORD)utf8.size());
    FlushFileBuffers(file);
    CloseHandle(file);

    if (!ok)
    {
        DeleteFileW(temp.c_str());
        return;
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(temp.c_str());
    }
}

// 距当前若干分钟的整分钟时刻（秒固定为 0）
static __int64 FtFromNowMinutes(int minutes)
{
    __int64 ft = FtNow() + (__int64)minutes * 60 * 10000000LL;
    SYSTEMTIME st = FtToSystemTime(ft);
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return SystemTimeToFt(st);
}

// 按开始时间升序插入，返回插入后的下标
static int AddExam(const std::wstring& name, __int64 startFt)
{
    if (g_examCount >= MAX_EXAMS) return -1;

    int pos = g_examCount;
    for (int i = 0; i < g_examCount; i++)
    {
        if (startFt < g_exam[i].startFt)
        {
            pos = i;
            break;
        }
    }

    for (int i = g_examCount; i > pos; i--)
    {
        g_exam[i] = g_exam[i - 1];
    }

    Exam& e = g_exam[pos];
    e.name = name;
    e.startFt = startFt;
    for (int k = 0; k < NODE_COUNT; k++)
    {
        e.nodeFt[k] = 0;
        e.nodeState[k] = ST_NONE;
    }
    g_examCount++;
    return pos;
}

static void RemoveExam(int index)
{
    if (index < 0 || index >= g_examCount) return;
    for (int i = index; i < g_examCount - 1; i++)
    {
        g_exam[i] = g_exam[i + 1];
    }
    g_examCount--;
    g_exam[g_examCount].name.clear();
    g_exam[g_examCount].startFt = 0;
}

static bool ParseDateTimeText(const std::wstring& raw, __int64* outFt)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    bool hasSeconds = false;

    if (swscanf(raw.c_str(), L"%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) hasSeconds = true;
    else if (swscanf(raw.c_str(), L"%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) return false;

    if (y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return false;
    if (!hasSeconds) s = 0;

    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)s;

    *outFt = SystemTimeToFt(st);
    return true;
}

static void LoadSettings()
{
    std::vector<std::wstring> names;
    std::vector<std::wstring> times;
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

                // 去掉可能的 UTF-8 BOM，避免首个键被污染
                if (!text.empty() && text[0] == (wchar_t)0xFEFF) text.erase(0, 1);

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
                    else if (key.size() >= 10 && key.compare(0, 4, L"exam") == 0)
                    {
                        // examN_name / examN_time
                        size_t p = 4;
                        int index = 0;
                        while (p < key.size() && key[p] >= L'0' && key[p] <= L'9')
                        {
                            index = index * 10 + (key[p] - L'0');
                            p++;
                        }
                        if (index >= MAX_EXAMS || p >= key.size()) continue;

                        while (index >= (int)names.size())
                        {
                            names.push_back(L"");
                            times.push_back(L"");
                        }

                        if (key.compare(p, key.size() - p, L"_name") == 0) names[index] = value;
                        else if (key.compare(p, key.size() - p, L"_time") == 0) times[index] = value;
                    }
                }
            }
        }

        CloseHandle(file);
    }

    for (int i = 0; i < 3; i++)
    {
        g_audio[i] = audioValue[i];
    }

    // 兼容旧版单场配置：只有 start 键
    if (names.empty() && !startValue.empty())
    {
        names.push_back(L"第 1 场");
        times.push_back(startValue);
    }

    for (int i = 0; i < (int)names.size() && g_examCount < MAX_EXAMS; i++)
    {
        __int64 ft = 0;
        if (!ParseDateTimeText(times[i], &ft)) continue;

        std::wstring name = names[i];
        if (name.empty()) name = L"第 " + std::to_wstring(g_examCount + 1) + L" 场";
        AddExam(name, ft);
    }

    if (g_examCount == 0 && !hasConfig)
    {
        // 首次运行：自动排一场，省去用户先添加的步骤
        AddExam(L"第 1 场", FtFromNowMinutes(30));
    }

    g_selectedExam = (g_examCount > 0) ? 0 : -1;
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
        if (ext != L".wav" && ext != L".mp3" && ext != L".wma" &&
            ext != L".mp4" && ext != L".m4a") continue;

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
// MP4 / M4A 等格式：MCI 的 mpegvideo 设备只认 MPEG-1，放不了 MP4
// 这里用系统自带的 Media Foundation（Windows 7 起随系统提供）补位
// ---------------------------------------------------------------------------
static bool g_mfReady = false;         // MFStartup 是否已成功
static IMFPMediaPlayer* g_mfPlayer = NULL;
static bool g_audioByMf = false;       // 当前响的是不是 MF 那一路

// 动态加载 Media Foundation：部分 N 版 / 精简版 Windows 没有 mfplay.dll，
// 若是静态导入，整个程序会直接启动失败。动态加载后最坏只是放不了 MP4。
typedef HRESULT (WINAPI* PFN_MFSTARTUP)(ULONG, DWORD);
typedef HRESULT (WINAPI* PFN_MFCREATE)(LPCWSTR, BOOL, MFP_CREATION_OPTIONS,
                                       IMFPMediaPlayerCallback*, HWND, IMFPMediaPlayer**);

static HMODULE g_mfplatDll = NULL, g_mfplayDll = NULL;
static PFN_MFSTARTUP     g_pMfStartup = NULL;
static PFN_MFCREATE      g_pMfCreate = NULL;

static bool ExtIs(const std::wstring& path, const wchar_t* ext)
{
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    return lstrcmpiW(path.c_str() + dot, ext) == 0;
}

static bool NeedMediaFoundation(const std::wstring& path)
{
    return ExtIs(path, L".mp4") || ExtIs(path, L".m4v") ||
           ExtIs(path, L".mov") || ExtIs(path, L".m4a") || ExtIs(path, L".aac");
}

static bool MfEnsure()
{
    if (g_mfReady) return true;

    if (!g_mfplatDll) g_mfplatDll = LoadLibraryW(L"mfplat.dll");
    if (!g_mfplayDll) g_mfplayDll = LoadLibraryW(L"mfplay.dll");
    if (!g_mfplatDll || !g_mfplayDll) return false;

    if (!g_pMfStartup) g_pMfStartup =
        (PFN_MFSTARTUP)GetProcAddress(g_mfplatDll, "MFStartup");
    if (!g_pMfCreate) g_pMfCreate =
        (PFN_MFCREATE)GetProcAddress(g_mfplayDll, "MFPCreateMediaPlayer");
    if (!g_pMfStartup || !g_pMfCreate) return false;

    // MFPlay 要求调用线程初始化过 COM
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    g_mfReady = SUCCEEDED(g_pMfStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
    return g_mfReady;
}

static void MfClose()
{
    if (g_mfPlayer)
    {
        g_mfPlayer->Stop();
        g_mfPlayer->Shutdown();
        g_mfPlayer->Release();
        g_mfPlayer = NULL;
    }
    g_audioByMf = false;
}

static bool MfIsPlaying()
{
    if (!g_mfPlayer) return false;

    MFP_MEDIAPLAYER_STATE state = MFP_MEDIAPLAYER_STATE_EMPTY;
    if (FAILED(g_mfPlayer->GetState(&state))) return false;
    return state == MFP_MEDIAPLAYER_STATE_PLAYING;
}

static bool MfPlay(const std::wstring& path)
{
    MfClose();
    if (!MfEnsure()) return false;

    // 直接用带 URL 的构造参数建播放器并要求立即播放。
    // 不走 SetMediaItem：MinGW 头文件把它的 fPlay 参数漏掉了，调用会错位。
    // 每个节点建一次播放器，开销完全可以接受。
    if (FAILED(g_pMfCreate(path.c_str(), TRUE, MFP_OPTION_NONE, NULL, NULL, &g_mfPlayer)))
    {
        g_mfPlayer = NULL;
        return false;
    }

    g_audioByMf = true;
    return true;
}

// 统一入口：按格式挑引擎，切换前先彻底停掉上一路，避免两路同时发声
static void AudioStopAll()
{
    MciClose();
    MfClose();
}

static bool AudioIsPlaying()
{
    return g_audioByMf ? MfIsPlaying() : MciIsPlaying();
}

static bool AudioPlay(const std::wstring& path)
{
    AudioStopAll();
    if (path.empty() || !FileExists(path)) return false;
    if (NeedMediaFoundation(path)) return MfPlay(path);
    return MciPlay(path);
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

// 自绘状态胶囊（与日程列表里的状态块保持一致的视觉）
static HWND MakeChip(const wchar_t* text, int x, int y, int w, int h, HFONT font)
{
    HWND control = CreateWindowExW(0, L"STATIC", text,
                                   WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                   S(x), S(y), S(w), S(h),
                                   g_hwnd, NULL, NULL, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
    return control;
}

static HWND MakeOwnedButton(const wchar_t* text, int x, int y, int w, int h,
                            BtnInfo* info, HFONT font, int id);

// 记录当前鼠标悬停的自绘按钮，用于 hover 着色
static HWND    g_hotBtn  = NULL;
static WNDPROC g_btnProc = NULL;

static LRESULT CALLBACK ButtonHoverProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (g_hotBtn != h && IsWindowEnabled(h))
        {
            HWND prev = g_hotBtn;
            g_hotBtn = h;
            if (prev) InvalidateRect(prev, NULL, FALSE);
            InvalidateRect(h, NULL, FALSE);

            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
        break;

    case WM_MOUSELEAVE:
        if (g_hotBtn == h)
        {
            g_hotBtn = NULL;
            InvalidateRect(h, NULL, FALSE);
        }
        break;

    case WM_NCDESTROY:
        if (g_hotBtn == h) g_hotBtn = NULL;
        break;
    }

    return CallWindowProcW(g_btnProc, h, msg, w, l);
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

    if (!g_btnProc) g_btnProc = (WNDPROC)GetWindowLongPtrW(b, GWLP_WNDPROC);
    SetWindowLongPtrW(b, GWLP_WNDPROC, (LONG_PTR)ButtonHoverProc);
    return b;
}

// 面板：白底 + 1px 边线 + 标题行 + 标题下分隔线（贴近桌面生产力软件的密集信息面板）
static void DrawCard(HDC dc, const CardRect& card, const wchar_t* title)
{
    RECT r;
    r.left = S(card.x);
    r.top = S(card.y);
    r.right = S(card.x + card.w);
    r.bottom = S(card.y + card.h);

    HBRUSH brush = CreateSolidBrush(C_SURFACE);
    HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
    HGDIOBJ b0 = SelectObject(dc, brush);
    HGDIOBJ p0 = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(10), S(10));
    SelectObject(dc, p0);
    SelectObject(dc, b0);
    DeleteObject(pen);
    DeleteObject(brush);

    // 标题
    HGDIOBJ oldFont = SelectObject(dc, fCap);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_MUTED);

    RECT tr = { r.left + S(14), r.top + S(9), r.right - S(14), r.top + S(31) };
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 标题下细线
    HPEN hair = CreatePen(PS_SOLID, 1, C_HAIR);
    HGDIOBJ hp = SelectObject(dc, hair);
    MoveToEx(dc, r.left + S(1), r.top + S(34), NULL);
    LineTo(dc, r.right - S(1), r.top + S(34));
    SelectObject(dc, hp);
    DeleteObject(hair);

    SelectObject(dc, oldFont);
}

// 给子控件补一圈圆角描边（控件本身去掉客户端边框）
static void DrawFieldFrame(HDC dc, HWND child)
{
    if (!child) return;

    RECT r;
    GetWindowRect(child, &r);
    POINT tl = { r.left, r.top };
    POINT br = { r.right, r.bottom };
    ScreenToClient(g_hwnd, &tl);
    ScreenToClient(g_hwnd, &br);

    r.left = tl.x;
    r.top = tl.y;
    r.right = br.x;
    r.bottom = br.y;
    InflateRect(&r, S(1), S(1));

    COLORREF line = C_LINE;

    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(8), S(8));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static WNDPROC g_editProc = NULL;

// 在名称输入框按回车等同于点击“添加到日程”
static LRESULT CALLBACK NameEditProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && w == VK_RETURN)
    {
        PostMessageW(g_hwnd, WM_COMMAND, MAKELONG(ID_ADD_EXAM, BN_CLICKED), 0);
        return 0;
    }
    if (msg == WM_CHAR && w == VK_RETURN) return 0;

    return CallWindowProcW(g_editProc, h, msg, w, l);
}

// ---------------------------------------------------------------------------
// 自绘时间选择器（不使用系统的 SysDateTimePick32）
//   左：日历图标，点击弹出月历
//   中：2026-10-30  09:00，点击选中某段，滚轮 / 上下箭头逐段调整
//   右：上下微调箭头
// -----------------------------------------------------------------------------------------------------------------------------------------
static HWND      g_calWnd = NULL;
static SYSTEMTIME g_pick;
static int       g_pickSeg = 0;
static int       g_segX[5] = {0, 0, 0, 0, 0};
static RECT      g_segRect[5];
static RECT      g_calIconRect, g_upRect, g_downRect;
static bool      g_hoverCal = false, g_hoverUp = false, g_hoverDown = false;
static ULONGLONG g_calClosedTick = 0;
static int       g_calYear = 0, g_calMonth = 0, g_calHoverDay = -1;
static RECT      g_calPrevRect, g_calNextRect;

static int DaysInMonth(int year, int month)
{
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2)
    {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

static void NormalizePick()
{
    int year = (int)g_pick.wYear;
    int month = (int)g_pick.wMonth;
    int day = (int)g_pick.wDay;
    int hour = (int)g_pick.wHour;
    int minute = (int)g_pick.wMinute;

    if (year < 1970) year = 1970;
    if (year > 2200) year = 2200;
    if (month < 1) month = 1;
    if (month > 12) month = 12;

    int dim = DaysInMonth(year, month);
    if (day < 1) day = 1;
    if (day > dim) day = dim;

    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;

    g_pick.wYear = (WORD)year;
    g_pick.wMonth = (WORD)month;
    g_pick.wDay = (WORD)day;
    g_pick.wHour = (WORD)hour;
    g_pick.wMinute = (WORD)minute;
    g_pick.wSecond = 0;
    g_pick.wMilliseconds = 0;
}

static void SetPickTime(__int64 ft)
{
    SYSTEMTIME st = FtToSystemTime(ft);
    g_pick = st;
    NormalizePick();
    if (hTimePick) InvalidateRect(hTimePick, NULL, FALSE);
}

static __int64 GetPickTime()
{
    if (g_pick.wYear < 1970) return 0;

    SYSTEMTIME st = g_pick;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return SystemTimeToFt(st);
}

static void PickParts(std::wstring parts[5])
{
    wchar_t buf[8];
    swprintf(buf, 8, L"%04d", (int)g_pick.wYear); parts[0] = buf;
    swprintf(buf, 8, L"%02d", (int)g_pick.wMonth); parts[1] = buf;
    swprintf(buf, 8, L"%02d", (int)g_pick.wDay); parts[2] = buf;
    swprintf(buf, 8, L"%02d", (int)g_pick.wHour); parts[3] = buf;
    swprintf(buf, 8, L"%02d", (int)g_pick.wMinute); parts[4] = buf;
}

static std::wstring PickFullText()
{
    std::wstring parts[5];
    PickParts(parts);
    return parts[0] + L"-" + parts[1] + L"-" + parts[2] + L"  " + parts[3] + L":" + parts[4];
}

static void AdjustPick(int delta)
{
    int year = (int)g_pick.wYear;
    int month = (int)g_pick.wMonth;
    int day = (int)g_pick.wDay;
    int hour = (int)g_pick.wHour;
    int minute = (int)g_pick.wMinute;

    switch (g_pickSeg)
    {
        case 0:
            year += delta;
            break;
        case 1:
            month += delta;
            if (month > 12) { month = 1; year++; }
            else if (month < 1) { month = 12; year--; }
            break;
        case 2:
            day += delta;
            if (day > DaysInMonth(year, month))
            {
                day = 1;
                month++;
                if (month > 12) { month = 1; year++; }
            }
            else if (day < 1)
            {
                month--;
                if (month < 1) { month = 12; year--; }
                day = DaysInMonth(year, month);
            }
            break;
        case 3:
            hour += delta;
            if (hour > 23) hour = 0;
            else if (hour < 0) hour = 23;
            break;
        case 4:
            minute += delta;
            if (minute > 59)
            {
                minute = 0;
                hour++;
                if (hour > 23) hour = 0;
            }
            else if (minute < 0)
            {
                minute = 59;
                hour--;
                if (hour < 0) hour = 23;
            }
            break;
    }

    g_pick.wYear = (WORD)year;
    g_pick.wMonth = (WORD)month;
    g_pick.wDay = (WORD)day;
    g_pick.wHour = (WORD)hour;
    g_pick.wMinute = (WORD)minute;
    NormalizePick();

    if (hTimePick) InvalidateRect(hTimePick, NULL, FALSE);
}

static void LayoutPicker(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);

    int iconSize = S(16);
    g_calIconRect.left = rc.left + S(9);
    g_calIconRect.top = (rc.top + rc.bottom - iconSize) / 2;
    g_calIconRect.right = g_calIconRect.left + iconSize;
    g_calIconRect.bottom = g_calIconRect.top + iconSize;

    int spinW = S(24);
    g_upRect.left = rc.right - spinW;
    g_upRect.right = rc.right - S(1);
    g_upRect.top = rc.top + S(1);
    g_upRect.bottom = (rc.top + rc.bottom) / 2;

    g_downRect.left = g_upRect.left;
    g_downRect.right = g_upRect.right;
    g_downRect.top = g_upRect.bottom;
    g_downRect.bottom = rc.bottom - S(1);

    std::wstring parts[5];
    PickParts(parts);
    std::wstring full = PickFullText();

    HDC dc = GetDC(h);
    HGDIOBJ oldFont = SelectObject(dc, fEdit);

    int textX = g_calIconRect.right + S(10);
    int top = (rc.top + rc.bottom) / 2 - S(10);
    int bottom = top + S(20);

    const int startOf[5] = { 0, 5, 8, 12, 15 };

    for (int i = 0; i < 5; i++)
    {
        SIZE prefix = {0, 0};
        if (startOf[i] > 0)
        {
            GetTextExtentPoint32W(dc, full.c_str(), startOf[i], &prefix);
        }

        SIZE seg = {0, 0};
        GetTextExtentPoint32W(dc, parts[i].c_str(), (int)parts[i].size(), &seg);

        g_segX[i] = textX + prefix.cx;
        g_segRect[i].left = textX + prefix.cx - S(3);
        g_segRect[i].right = textX + prefix.cx + seg.cx + S(3);
        g_segRect[i].top = top;
        g_segRect[i].bottom = bottom;
    }

    SelectObject(dc, oldFont);
    ReleaseDC(h, dc);
}

static void PaintPicker(HWND h)
{
    LayoutPicker(h);

    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);

    RECT rc;
    GetClientRect(h, &rc);

    FillRect(dc, &rc, bSurface);

    bool focused = (GetFocus() == h);

    // 圆角描边（与输入框、按钮统一）
    HPEN border = CreatePen(PS_SOLID, 1, focused ? C_GREEN : C_LINE);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, S(8), S(8));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);

    // 选中段的浅绿底
    if (g_pickSeg >= 0 && g_pickSeg < 5)
    {
        RECT sel = g_segRect[g_pickSeg];
        sel.top += S(3);
        sel.bottom -= S(3);

        HGDIOBJ sb = SelectObject(dc, bSel);
        HGDIOBJ sp2 = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, sel.left, sel.top, sel.right, sel.bottom, S(6), S(6));
        SelectObject(dc, sp2);
        SelectObject(dc, sb);
    }

    // 日历图标
    COLORREF iconColor = g_hoverCal ? C_GREEN : C_MUTED;
    HPEN iconPen = CreatePen(PS_SOLID, 1, iconColor);
    HGDIOBJ prevPen = SelectObject(dc, iconPen);
    HGDIOBJ prevBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, g_calIconRect.left, g_calIconRect.top + S(3),
                 g_calIconRect.right, g_calIconRect.bottom);
    MoveToEx(dc, g_calIconRect.left, g_calIconRect.top + S(7), NULL);
    LineTo(dc, g_calIconRect.right, g_calIconRect.top + S(7));
    MoveToEx(dc, g_calIconRect.left + S(4), g_calIconRect.top, NULL);
    LineTo(dc, g_calIconRect.left + S(4), g_calIconRect.top + S(5));
    MoveToEx(dc, g_calIconRect.right - S(4), g_calIconRect.top, NULL);
    LineTo(dc, g_calIconRect.right - S(4), g_calIconRect.top + S(5));
    SelectObject(dc, prevBrush);
    SelectObject(dc, prevPen);
    DeleteObject(iconPen);

    // 文本
    HGDIOBJ oldFont = SelectObject(dc, fEdit);

    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int textY = (rc.top + rc.bottom - tm.tmHeight) / 2;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_INK);
    TextOutW(dc, g_segX[0], textY, PickFullText().c_str(), (int)PickFullText().size());

    std::wstring parts[5];
    PickParts(parts);
    SetTextColor(dc, C_GREEN);
    TextOutW(dc, g_segX[g_pickSeg], textY, parts[g_pickSeg].c_str(), (int)parts[g_pickSeg].size());

    SelectObject(dc, oldFont);

    // 微调箭头
    HPEN sepPen = CreatePen(PS_SOLID, 1, C_LINE);
    HGDIOBJ sp = SelectObject(dc, sepPen);
    MoveToEx(dc, g_upRect.left, g_upRect.top + S(2), NULL);
    LineTo(dc, g_upRect.left, g_downRect.bottom - S(2));
    SelectObject(dc, sp);
    DeleteObject(sepPen);

    int cxUp = (g_upRect.left + g_upRect.right) / 2;
    int cyUp = (g_upRect.top + g_upRect.bottom) / 2;
    int cxDown = cxUp;
    int cyDown = (g_downRect.top + g_downRect.bottom) / 2;

    COLORREF upColor = g_hoverUp ? C_GREEN : C_MUTED;
    COLORREF downColor = g_hoverDown ? C_GREEN : C_MUTED;

    POINT upTri[3] = {
        { cxUp - S(4), cyUp + S(2) },
        { cxUp + S(4), cyUp + S(2) },
        { cxUp,        cyUp - S(3) }
    };
    POINT downTri[3] = {
        { cxDown - S(4), cyDown - S(2) },
        { cxDown + S(4), cyDown - S(2) },
        { cxDown,        cyDown + S(3) }
    };

    HBRUSH upBrush = CreateSolidBrush(upColor);
    HBRUSH downBrush = CreateSolidBrush(downColor);

    HGDIOBJ b0 = SelectObject(dc, upBrush);
    HGDIOBJ b1 = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, upTri, 3);
    SelectObject(dc, downBrush);
    Polygon(dc, downTri, 3);
    SelectObject(dc, b1);
    SelectObject(dc, b0);

    DeleteObject(upBrush);
    DeleteObject(downBrush);

    EndPaint(h, &ps);
}

static void CalPrevNextRects()
{
    RECT rc;
    GetClientRect(g_calWnd, &rc);

    int size = S(24);
    int top = S(7);

    g_calPrevRect.left = rc.left + S(8);
    g_calPrevRect.top = top;
    g_calPrevRect.right = g_calPrevRect.left + size;
    g_calPrevRect.bottom = top + size;

    g_calNextRect.right = rc.right - S(8);
    g_calNextRect.top = top;
    g_calNextRect.left = g_calNextRect.right - size;
    g_calNextRect.bottom = top + size;
}

static int CalFirstSlot()
{
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)g_calYear;
    st.wMonth = (WORD)g_calMonth;
    st.wDay = 1;

    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);

    SYSTEMTIME out;
    FileTimeToSystemTime(&ft, &out);

    return (out.wDayOfWeek + 6) % 7;   // 周一为 0
}

static RECT CalSlotRect(int slot)
{
    RECT rc;
    GetClientRect(g_calWnd, &rc);

    int left = S(10);
    int top = S(62);
    int cellW = (rc.right - left - S(10)) / 7;
    int cellH = S(28);

    int row = slot / 7;
    int col = slot % 7;

    RECT r;
    r.left = left + col * cellW + S(2);
    r.right = r.left + cellW - S(4);
    r.top = top + row * cellH + S(2);
    r.bottom = r.top + cellH - S(4);
    return r;
}

static void CalShiftMonth(int delta)
{
    g_calMonth += delta;
    if (g_calMonth > 12) { g_calMonth = 1; g_calYear++; }
    else if (g_calMonth < 1) { g_calMonth = 12; g_calYear--; }

    if (g_calYear < 1970) g_calYear = 1970;
    if (g_calYear > 2200) g_calYear = 2200;

    g_calHoverDay = -1;
    InvalidateRect(g_calWnd, NULL, TRUE);
}

static void PaintCalendar()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_calWnd, &ps);

    RECT rc;
    GetClientRect(g_calWnd, &rc);

    // 弹层白底 + 圆角描边
    FillRect(dc, &rc, bSurface);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(214, 219, 226));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, S(12), S(12));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    CalPrevNextRects();

    SetBkMode(dc, TRANSPARENT);

    // 标题
    wchar_t title[64];
    swprintf(title, 64, L"%d 年 %d 月", g_calYear, g_calMonth);

    HGDIOBJ oldFont = SelectObject(dc, fCap);
    SetTextColor(dc, C_INK);

    RECT titleRect = { rc.left + S(40), S(9), rc.right - S(40), S(33) };
    DrawTextW(dc, title, -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 左右翻月箭头
    int cy = (g_calPrevRect.top + g_calPrevRect.bottom) / 2;
    int cxL = (g_calPrevRect.left + g_calPrevRect.right) / 2;
    int cxR = (g_calNextRect.left + g_calNextRect.right) / 2;

    POINT leftTri[3] = { { cxL + S(3), cy - S(4) }, { cxL + S(3), cy + S(4) }, { cxL - S(3), cy } };
    POINT rightTri[3] = { { cxR - S(3), cy - S(4) }, { cxR - S(3), cy + S(4) }, { cxR + S(3), cy } };

    HBRUSH arrowBrush = CreateSolidBrush(C_MUTED);
    HGDIOBJ ab = SelectObject(dc, arrowBrush);
    HGDIOBJ ap = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, leftTri, 3);
    Polygon(dc, rightTri, 3);
    SelectObject(dc, ap);
    SelectObject(dc, ab);
    DeleteObject(arrowBrush);

    // 星期表头
    static const wchar_t* week[7] = { L"一", L"二", L"三", L"四", L"五", L"六", L"日" };

    SelectObject(dc, fState);
    SetTextColor(dc, RGB(150, 157, 167));

    int gridLeft = S(10);
    int cellW = (rc.right - gridLeft - S(10)) / 7;

    for (int i = 0; i < 7; i++)
    {
        RECT r;
        r.left = gridLeft + i * cellW;
        r.right = r.left + cellW;
        r.top = S(36);
        r.bottom = S(58);
        DrawTextW(dc, week[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 日期格：仅选中 / 今天 / 悬停时上色，其余留白
    int first = CalFirstSlot();
    int dim = DaysInMonth(g_calYear, g_calMonth);

    SYSTEMTIME today = FtToSystemTime(FtNow());

    for (int d = 1; d <= dim; d++)
    {
        int slot = first + d - 1;
        if (slot >= 42) break;

        RECT r = CalSlotRect(slot);
        bool selected = (g_pick.wYear == g_calYear && g_pick.wMonth == g_calMonth && g_pick.wDay == d);
        bool isToday = (today.wYear == g_calYear && today.wMonth == g_calMonth && today.wDay == d);
        bool hover = (g_calHoverDay == d);

        if (selected || hover || isToday)
        {
            RECT mark = r;
            mark.top += S(1);
            mark.bottom -= S(1);

            COLORREF fill = selected ? C_GREEN : (hover ? RGB(234, 243, 238) : C_SURFACE);
            HBRUSH cellBrush = CreateSolidBrush(fill);

            HPEN cellPen = NULL;
            if (!selected && isToday && !hover)
            {
                cellPen = CreatePen(PS_SOLID, 1, C_GREEN);
            }

            HGDIOBJ cb = SelectObject(dc, cellBrush);
            HGDIOBJ cp = SelectObject(dc, cellPen ? cellPen : GetStockObject(NULL_PEN));
            RoundRect(dc, mark.left, mark.top, mark.right, mark.bottom, S(8), S(8));
            SelectObject(dc, cp);
            SelectObject(dc, cb);
            DeleteObject(cellBrush);
            if (cellPen) DeleteObject(cellPen);
        }

        wchar_t dayText[8];
        swprintf(dayText, 8, L"%d", d);

        SelectObject(dc, fState);
        SetTextColor(dc, selected ? C_SURFACE : (isToday ? C_GREEN : C_INK));

        RECT textRect = r;
        DrawTextW(dc, dayText, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, oldFont);
    EndPaint(g_calWnd, &ps);
}

static void CloseCalendar()
{
    if (!g_calWnd) return;

    HWND w = g_calWnd;
    g_calWnd = NULL;
    g_calClosedTick = GetTickCount64();
    DestroyWindow(w);
}

static void ToggleCalendar()
{
    if (g_calWnd)
    {
        CloseCalendar();
        return;
    }

    // 刚刚因拾取焦点而关闭时，忽略这次点击，避免“关了又开”
    if (GetTickCount64() - g_calClosedTick < 400) return;

    g_calYear = (int)g_pick.wYear;
    g_calMonth = (int)g_pick.wMonth;
    g_calHoverDay = -1;

    RECT pr;
    GetWindowRect(hTimePick, &pr);

    int w = S(266);
    int h = S(246);
    int x = pr.left;
    int y = pr.bottom + S(4);

    HMONITOR mon = MonitorFromRect(&pr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi))
    {
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y + h > mi.rcWork.bottom) y = pr.top - h - S(4);
        if (y < mi.rcWork.top) y = mi.rcWork.top;
    }

    g_calWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EBCalendar", L"",
                               WS_POPUP, x, y, w, h, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_calWnd) return;

    // 圆角窗口
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, S(14), S(14));
    SetWindowRgn(g_calWnd, region, TRUE);

    ShowWindow(g_calWnd, SW_SHOW);
    SetFocus(g_calWnd);
}

static void CalendarHoverAt(POINT pt)
{
    int day = -1;

    int first = CalFirstSlot();
    int dim = DaysInMonth(g_calYear, g_calMonth);

    for (int d = 1; d <= dim; d++)
    {
        int slot = first + d - 1;
        if (slot >= 42) break;

        RECT r = CalSlotRect(slot);
        if (PtInRect(&r, pt)) { day = d; break; }
    }

    if (day != g_calHoverDay)
    {
        g_calHoverDay = day;
        InvalidateRect(g_calWnd, NULL, FALSE);
    }
}

static LRESULT CALLBACK CalendarProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
        case WM_PAINT:
            PaintCalendar();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE) CloseCalendar();
            return 0;

        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            CalPrevNextRects();

            if (PtInRect(&g_calPrevRect, pt)) { CalShiftMonth(-1); return 0; }
            if (PtInRect(&g_calNextRect, pt)) { CalShiftMonth(1); return 0; }

            int first = CalFirstSlot();
            int dim = DaysInMonth(g_calYear, g_calMonth);

            for (int d = 1; d <= dim; d++)
            {
                int slot = first + d - 1;
                if (slot >= 42) break;

                RECT r = CalSlotRect(slot);
                if (!PtInRect(&r, pt)) continue;

                g_pick.wYear = (WORD)g_calYear;
                g_pick.wMonth = (WORD)g_calMonth;
                g_pick.wDay = (WORD)d;
                NormalizePick();

                if (hTimePick) InvalidateRect(hTimePick, NULL, FALSE);
                CloseCalendar();
                return 0;
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            CalendarHoverAt(pt);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_calHoverDay != -1)
            {
                g_calHoverDay = -1;
                InvalidateRect(g_calWnd, NULL, FALSE);
            }
            return 0;

        case WM_MOUSEWHEEL:
            CalShiftMonth(GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1);
            return 0;

        case WM_KEYDOWN:
            if (w == VK_ESCAPE) { CloseCalendar(); return 0; }
            if (w == VK_LEFT)   { CalShiftMonth(-1); return 0; }
            if (w == VK_RIGHT)  { CalShiftMonth(1); return 0; }
            if (w == VK_PRIOR)  { CalShiftMonth(-1); return 0; }
            if (w == VK_NEXT)   { CalShiftMonth(1); return 0; }
            return 0;
    }

    return DefWindowProcW(h, msg, w, l);
}

static LRESULT CALLBACK TimePickProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(h, NULL, FALSE);
            return 0;

        case WM_LBUTTONDOWN:
        {
            SetFocus(h);

            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };

            if (g_calWnd)
            {
                CloseCalendar();
                return 0;
            }

            if (PtInRect(&g_calIconRect, pt)) { ToggleCalendar(); return 0; }
            if (PtInRect(&g_upRect, pt))      { AdjustPick(1);   return 0; }
            if (PtInRect(&g_downRect, pt))    { AdjustPick(-1);  return 0; }

            for (int i = 0; i < 5; i++)
            {
                if (PtInRect(&g_segRect[i], pt))
                {
                    g_pickSeg = i;
                    InvalidateRect(h, NULL, FALSE);
                    break;
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
            AdjustPick(GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1);
            return 0;

        case WM_MOUSEMOVE:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };

            bool hc = PtInRect(&g_calIconRect, pt) != 0;
            bool hu = PtInRect(&g_upRect, pt) != 0;
            bool hd = PtInRect(&g_downRect, pt) != 0;

            if (hc != g_hoverCal || hu != g_hoverUp || hd != g_hoverDown)
            {
                g_hoverCal = hc;
                g_hoverUp = hu;
                g_hoverDown = hd;
                InvalidateRect(h, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_hoverCal || g_hoverUp || g_hoverDown)
            {
                g_hoverCal = g_hoverUp = g_hoverDown = false;
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;

        case WM_SETCURSOR:
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);

            if (PtInRect(&g_calIconRect, pt) || PtInRect(&g_upRect, pt) || PtInRect(&g_downRect, pt))
            {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_KEYDOWN:
            if (w == VK_LEFT)  { g_pickSeg = (g_pickSeg + 4) % 5; InvalidateRect(h, NULL, FALSE); return 0; }
            if (w == VK_RIGHT) { g_pickSeg = (g_pickSeg + 1) % 5; InvalidateRect(h, NULL, FALSE); return 0; }
            if (w == VK_UP)    { AdjustPick(1);  return 0; }
            if (w == VK_DOWN)  { AdjustPick(-1); return 0; }
            if (w == VK_NEXT)  { AdjustPick(-5); return 0; }
            if (w == VK_PRIOR) { AdjustPick(5);  return 0; }
            break;

        case WM_PAINT:
            PaintPicker(h);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            CloseCalendar();
            return 0;
    }

    return DefWindowProcW(h, msg, w, l);
}

static void BuildUi()
{
    // 字体
    fTitle   = MakeFont(155, true);
    fSub     = MakeFont(85,  false);
    fClock   = MakeFont(190, true);
    fDate    = MakeFont(85,  false);
    fSync    = MakeFont(85,  false);
    fCap     = MakeFont(95,  true);
    fEdit    = MakeFont(105, false);
    fRow     = MakeFont(95,  false);
    fState   = MakeFont(85,  false);
    fHint    = MakeFont(85,  false);
    fLog     = MakeFont(85,  false);
    fBtnTool = MakeFont(90,  false);
    fStart   = MakeFont(145, true);
    fStop    = MakeFont(150, true);

    // 刷子
    bBg         = CreateSolidBrush(C_BG);
    bSurface    = CreateSolidBrush(C_SURFACE);
    bLine       = CreateSolidBrush(C_LINE);
    bEdit       = CreateSolidBrush(C_SURFACE);
    bSel        = CreateSolidBrush(RGB(234, 243, 238));
    bLog        = CreateSolidBrush(RGB(250, 251, 252));
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

    // 顶部：左侧标题与运行状态同排，副标题在下方；右侧时钟与校时
    hTitle     = MakeStatic(L"考试自动打铃", 22, 10, 200, 30, fTitle, SS_LEFT);
    hCountdown = MakeChip(L"待启动", 232, 13, 170, 26, fState);
    hSubtitle  = MakeStatic(L"多场考试 · 北京时间基准 · 三段音频自动播放", 24, 46, 520, 20, fSub, SS_LEFT);

    hClock = MakeStatic(L"--:--:--", 596, 2, 404, 38, fClock, SS_RIGHT);
    hDate  = MakeStatic(L"北京时间", 616, 41, 384, 18, fDate, SS_RIGHT);
    hSync  = MakeStatic(L"正在校时…", 616, 59, 264, 18, fSync, SS_RIGHT);
    hBtnSync = MakeOwnedButton(L"重新校时", 888, 54, 112, 26, &biTool, fBtnTool, ID_SYNC);

    // ------------------------------------------------------------------
    // 考试日程
    // ------------------------------------------------------------------
    hEditName = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
                                S(kCardExam.x + 14), S(kCardExam.y + 38), S(180), S(30),
                                g_hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);
    SendMessageW(hEditName, WM_SETFONT, (WPARAM)fEdit, TRUE);
    SendMessageW(hEditName, EM_SETLIMITTEXT, 40, 0);
    SendMessageW(hEditName, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(S(8), S(8)));
    SendMessageW(hEditName, EM_SETCUEBANNER, TRUE, (LPARAM)L"考试名称");
    g_editProc = (WNDPROC)SetWindowLongPtrW(hEditName, GWLP_WNDPROC, (LONG_PTR)NameEditProc);

    hTimePick = CreateWindowExW(0, L"EBTimePick", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                S(kCardExam.x + 202), S(kCardExam.y + 38), S(288), S(30),
                                g_hwnd, (HMENU)ID_TIME_PICK, NULL, NULL);
    SendMessageW(hTimePick, WM_SETFONT, (WPARAM)fEdit, TRUE);
    SetPickTime(FtFromNowMinutes(30));

    hBtnAdd = MakeOwnedButton(L"添加到日程", kCardExam.x + 498, kCardExam.y + 38, 114, 30,
                              &biPrimary, fBtnTool, ID_ADD_EXAM);

    hSetHint = MakeStatic(L"点日历图标选日期，点数字段后用滚轮或右侧箭头微调；启动监测后仍可继续添加",
                          kCardExam.x + 14, kCardExam.y + 72, 600, 18, fHint, SS_LEFT);

    hExamList = CreateWindowExW(0, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                                S(kCardExam.x + 14), S(kCardExam.y + 96), S(612), S(152),
                                g_hwnd, (HMENU)ID_EXAM_LIST, NULL, NULL);
    SendMessageW(hExamList, WM_SETFONT, (WPARAM)fRow, TRUE);
    SendMessageW(hExamList, LB_SETITEMHEIGHT, 0, (LPARAM)S(48));

    hBtnDel   = MakeOwnedButton(L"删除选中", kCardExam.x + 14, kCardExam.y + 258, 100, 30, &biTool, fBtnTool, ID_DEL_EXAM);
    hBtnClear = MakeOwnedButton(L"清空日程", kCardExam.x + 122, kCardExam.y + 258, 88, 30, &biTool, fBtnTool, ID_CLEAR_EXAM);
    hExamCount = MakeStatic(L"共 0 场", kCardExam.x + 400, kCardExam.y + 9, 226, 18, fState, SS_RIGHT);

    // ------------------------------------------------------------------
    // 打铃节点
    // ------------------------------------------------------------------
    hNodeTitle = MakeStatic(L"未选择场次", kCardNode.x + 180, kCardNode.y + 12, 446, 20, fState, SS_RIGHT);

    MakeStatic(L"节点", kCardNode.x + 14, kCardNode.y + 42, 200, 18, fState, SS_LEFT);
    MakeStatic(L"预定时间", kCardNode.x + 336, kCardNode.y + 42, 130, 18, fState, SS_LEFT);
    MakeStatic(L"状态", kCardNode.x + 484, kCardNode.y + 42, 110, 18, fState, SS_LEFT);

    for (int i = 0; i < NODE_COUNT; i++)
    {
        int y = kCardNode.y + 72 + i * 50;
        hNodeName[i]  = MakeStatic(kNodeName[i], kCardNode.x + 14, y, 310, 24, fRow, SS_LEFT);
        hNodeTime[i]  = MakeStatic(L"—", kCardNode.x + 336, y, 130, 24, fRow, SS_LEFT);
        hNodeState[i] = MakeChip(L"未排期", kCardNode.x + 480, y, 120, 24, fState);
    }

    // ------------------------------------------------------------------
    // 音频文件
    // ------------------------------------------------------------------
    for (int i = 0; i < NODE_COUNT; i++)
    {
        int y = kCardAudio.y + 42 + i * 50;

        // 左侧上下两行：节点名 + 文件名
        hAudioCap[i]  = MakeStatic(kNodeShort[i], kCardAudio.x + 14, y, 96, 20, fRow, SS_LEFT);
        hAudioFile[i] = MakeStatic(L"未选择", kCardAudio.x + 14, y + 20, 180, 18,
                                   fState, SS_LEFT | SS_ENDELLIPSIS);

        // 右侧两个按钮
        hBtnPick[i] = MakeOwnedButton(L"选择", kCardAudio.x + 202, y + 7, 50, 26, &biTool, fBtnTool, ID_PICK0 + i);
        hBtnPlay[i] = MakeOwnedButton(L"试听", kCardAudio.x + 258, y + 7, 50, 26, &biTool, fBtnTool, ID_PLAY0 + i);
    }

    // ------------------------------------------------------------------
    // 运行日志
    // ------------------------------------------------------------------
    hLog = CreateWindowExW(0, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                           S(kCardLog.x + 14), S(kCardLog.y + 40), S(296), S(286),
                           g_hwnd, (HMENU)ID_LOG, NULL, NULL);
    SendMessageW(hLog, WM_SETFONT, (WPARAM)fLog, TRUE);

    // 底部操作条：主操作靠左，急停靠右，形成清晰的主次层级
    hBtnStart = MakeOwnedButton(L"启动监测", 20, 684, 240, 48, &biPrimary, fStart, ID_START);
    hBtnStop  = MakeOwnedButton(L"急停", 856, 684, 144, 48, &biDanger, fStop, ID_STOP);
    hFootHint = MakeStatic(L"按 Esc 亦可急停", 272, 697, 572, 22, fHint, SS_LEFT);
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
    return L"未开始";
}

// 单场考试的汇总状态：只要还有节点在播/待触发，就以外层为准
static int ExamAggState(int exam)
{
    if (exam < 0 || exam >= g_examCount) return ST_NONE;

    bool anyPend = false, anyPlay = false, anyCnc = false, anyMiss = false, anyDone = false;
    for (int k = 0; k < NODE_COUNT; k++)
    {
        switch (g_exam[exam].nodeState[k])
        {
            case ST_PEND: anyPend = true; break;
            case ST_PLAY: anyPlay = true; break;
            case ST_CANC: anyCnc  = true; break;
            case ST_MISS: anyMiss = true; break;
            case ST_DONE: anyDone = true; break;
        }
    }

    if (anyPlay) return ST_PLAY;
    if (anyPend) return ST_PEND;
    if (anyCnc)  return ST_CANC;
    if (anyMiss) return ST_MISS;
    if (anyDone) return ST_DONE;
    return ST_NONE;
}

// 未排期但已过预定时刻的场次，界面上按“已过期”呈现
static int ExamDisplayState(int exam)
{
    int state = ExamAggState(exam);
    if (state != ST_NONE) return state;

    if (exam >= 0 && exam < g_examCount &&
        FtNow() - g_exam[exam].startFt > 60LL * 10000000LL)
    {
        return ST_MISS;
    }
    return ST_NONE;
}

static const wchar_t* ExamStatusText(int exam)
{
    switch (ExamDisplayState(exam))
    {
        case ST_PEND: return L"待触发";
        case ST_PLAY: return L"播放中";
        case ST_DONE: return L"已完成";
        case ST_MISS: return L"已过期";
        case ST_CANC: return L"已取消";
    }
    return L"未开始";
}

// 选中场次当前展示的节点状态
static int NodeStateAt(int kind)
{
    if (g_selectedExam < 0 || g_selectedExam >= g_examCount) return ST_NONE;
    if (kind < 0 || kind >= NODE_COUNT) return ST_NONE;

    const Exam& e = g_exam[g_selectedExam];
    if (e.nodeState[kind] != ST_NONE) return e.nodeState[kind];

    __int64 ft = e.startFt + (__int64)kNodeOffsetMin[kind] * 60LL * 10000000LL;
    if (FtNow() - ft > 60LL * 10000000LL) return ST_MISS;
    return ST_NONE;
}

static void UpdateNodePanel()
{
    int exam = g_selectedExam;

    if (exam < 0 || exam >= g_examCount)
    {
        SetWindowTextW(hNodeTitle, L"未选择场次");

        for (int i = 0; i < NODE_COUNT; i++)
        {
            SetWindowTextW(hNodeTime[i], L"—");
            SetWindowTextW(hNodeState[i], L"未排期");
            InvalidateRect(hNodeState[i], NULL, TRUE);
        }
        return;
    }

    const Exam& e = g_exam[exam];
    SetWindowTextW(hNodeTitle, (L"当前查看：" + e.name).c_str());

    const __int64 minute = 60LL * 10000000LL;

    for (int i = 0; i < NODE_COUNT; i++)
    {
        int state = NodeStateAt(i);
        __int64 ft = (e.nodeState[i] == ST_NONE)
                     ? e.startFt + (__int64)kNodeOffsetMin[i] * minute
                     : e.nodeFt[i];

        SetWindowTextW(hNodeTime[i], FormatMonthDayHm(ft).c_str());
        SetWindowTextW(hNodeState[i],
                       (state == ST_NONE) ? L"未开始" : StateText(state));
        InvalidateRect(hNodeState[i], NULL, TRUE);
        InvalidateRect(hNodeTime[i], NULL, TRUE);
    }
}

static void UpdateAllNodeRows()
{
    UpdateNodePanel();
    InvalidateRect(hExamList, NULL, TRUE);
}

static void UpdateAudioLabel(int index)
{
    if (g_audio[index].empty() || !FileExists(g_audio[index]))
    {
        if (g_audio[index].empty())
        {
            SetWindowTextW(hAudioFile[index], L"未选择");
        }
        else
        {
            SetWindowTextW(hAudioFile[index], (L"缺失：" + FileNameOf(g_audio[index])).c_str());
        }
    }
    else
    {
        SetWindowTextW(hAudioFile[index], FileNameOf(g_audio[index]).c_str());
    }
    InvalidateRect(hAudioFile[index], NULL, TRUE);
}

static void RefreshExamList()
{
    SendMessageW(hExamList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hExamList, LB_RESETCONTENT, 0, 0);

    for (int i = 0; i < g_examCount; i++)
    {
        SendMessageW(hExamList, LB_ADDSTRING, 0, (LPARAM)g_exam[i].name.c_str());
    }

    SendMessageW(hExamList, WM_SETREDRAW, TRUE, 0);

    if (g_selectedExam >= 0 && g_selectedExam < g_examCount)
    {
        SendMessageW(hExamList, LB_SETCURSEL, g_selectedExam, 0);
    }
    else
    {
        g_selectedExam = (g_examCount > 0) ? 0 : -1;
        if (g_selectedExam >= 0) SendMessageW(hExamList, LB_SETCURSEL, 0, 0);
    }

    // 场次数展示在标题行右侧
    wchar_t buf[64];
    swprintf(buf, 64, L"共 %d 场%s", g_examCount,
             g_examCount == 0 ? L" · 请先添加" : L"");
    SetWindowTextW(hExamCount, buf);

    // 空日程时隐藏列表，改由主窗口绘制占位提示
    ShowWindow(hExamList, g_examCount == 0 ? SW_HIDE : SW_SHOW);
    InvalidateRect(g_hwnd, NULL, FALSE);

    if (!g_running)
    {
        EnableWindow(hBtnDel, g_examCount > 0);
        EnableWindow(hBtnClear, g_examCount > 0);
    }

    InvalidateRect(hExamList, NULL, TRUE);
    UpdateNodePanel();
}

static void UpdateCountdown()
{
    std::wstring text;

    if (g_running)
    {
        int bestExam = -1, bestKind = -1;
        for (int i = 0; i < g_examCount; i++)
        {
            for (int k = 0; k < NODE_COUNT; k++)
            {
                if (g_exam[i].nodeState[k] != ST_PEND) continue;
                if (bestExam < 0 || g_exam[i].nodeFt[k] < g_exam[bestExam].nodeFt[bestKind])
                {
                    bestExam = i;
                    bestKind = k;
                }
            }
        }

        if (bestExam < 0)
        {
            text = L"本轮已完成";
            g_runChip = 3;
        }
        else
        {
            __int64 remain = g_exam[bestExam].nodeFt[bestKind] - FtNow();
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
            text = buf;
            g_runChip = (totalSeconds <= 60) ? 2 : 1;
        }
    }
    else if (g_stoppedByUser)
    {
        text = L"已急停";
        g_runChip = 4;
    }
    else
    {
        text = L"待启动";
        g_runChip = 0;
    }

    // 仅在文案或配色变化时重绘，空闲时几乎不产生绘制
    static std::wstring lastCountText;
    static int lastRunChip = -1;
    if (lastCountText != text || lastRunChip != g_runChip)
    {
        lastCountText = text;
        lastRunChip = g_runChip;
        SetWindowTextW(hCountdown, text.c_str());

        // 胶囊宽度跟随文案，避免短文案拖着一条长底
        if (hCountdown && fState)
        {
            HDC dc = GetDC(hCountdown);
            HGDIOBJ old = SelectObject(dc, fState);
            SIZE sz = {0, 0};
            GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
            SelectObject(dc, old);
            ReleaseDC(hCountdown, dc);

            int w = S(26) + S(7) + sz.cx + S(14);
            if (w < S(96)) w = S(96);

            RECT rc;
            GetWindowRect(hCountdown, &rc);
            POINT tl = { rc.left, rc.top };
            ScreenToClient(g_hwnd, &tl);

            SetWindowPos(hCountdown, NULL, tl.x, tl.y, w, S(26),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

        InvalidateRect(hCountdown, NULL, TRUE);
    }
}

// ---------------------------------------------------------------------------
// 打铃调度
// ---------------------------------------------------------------------------
// 同一时刻可能有多个节点到点（多场考试同时开始，或上一段还没播完）。
// 早期实现会直接打断上一段，导致播报被截断；这里改为排队，逐段放完。
struct BellItem { int exam; int kind; };

static std::vector<BellItem> g_bellQueue;
static ULONGLONG g_playStartTick = 0;   // 本段开始播放的时刻，用于看门狗
static bool      g_playVerified  = false;

// 真正启动一段播放；返回 false 表示应改用提示音兜底
static bool StartBellAudio(int kind)
{
    std::wstring file = g_audio[kind];
    if (file.empty())
    {
        Log(L"未设置音频文件，改用系统提示音");
        return false;
    }

    if (!FileExists(file))
    {
        Log(L"音频文件不存在：" + file);
        return false;
    }

    if (!AudioPlay(file))
    {
        Log(L"音频播放失败，改用系统提示音");
        return false;
    }

    return true;
}

static void PumpBellQueue()
{
    if (g_playExam >= 0) return;   // 正在播，等它结束后再取下一段

    while (!g_bellQueue.empty())
    {
        BellItem item = g_bellQueue.front();
        g_bellQueue.erase(g_bellQueue.begin());

        if (item.exam < 0 || item.exam >= g_examCount) continue;

        if (StartBellAudio(item.kind))
        {
            g_playExam      = item.exam;
            g_playKind      = item.kind;
            g_playStartTick = GetTickCount64();
            g_playVerified  = false;
            UpdateAllNodeRows();
            return;
        }

        // 兜底：系统提示音，并立即收尾，不能因为没声音就卡住队列
        MessageBeep(MB_ICONEXCLAMATION);

        if (g_exam[item.exam].nodeState[item.kind] == ST_PLAY)
        {
            g_exam[item.exam].nodeState[item.kind] = ST_DONE;
        }
        UpdateAllNodeRows();
    }
}

static void TriggerBell(int exam, int kind)
{
    if (exam < 0 || exam >= g_examCount) return;
    if (kind < 0 || kind >= NODE_COUNT) return;

    Exam& e = g_exam[exam];

    e.nodeState[kind] = ST_PLAY;
    UpdateAllNodeRows();
    Log(L"打铃：" + e.name + L" · " + NodeName(kind));

    g_bellQueue.push_back(BellItem{ exam, kind });
    PumpBellQueue();
}

// 监测期间阻止系统休眠 / 关闭显示器，避免到点不响
static void SetAwake(bool on);

// 空闲时降低轮询频率（1s），监测中保持 200ms，兼顾精度与功耗
static void ApplyTimerMode();

static void CheckAllFinished()
{
    for (int i = 0; i < g_examCount; i++)
    {
        for (int k = 0; k < NODE_COUNT; k++)
        {
            if (g_exam[i].nodeState[k] == ST_PEND || g_exam[i].nodeState[k] == ST_PLAY) return;
        }
    }

    if (!g_running) return;

    g_running = false;
    SetAwake(false);
    EnableWindow(hBtnStart, TRUE);
    SetWindowTextW(hBtnStart, L"启动监测");
    InvalidateRect(hBtnStart, NULL, TRUE);

    EnableWindow(hBtnDel, g_examCount > 0);
    EnableWindow(hBtnClear, g_examCount > 0);

    Log(L"全部场次任务已结束，可继续添加或重新启动");
    UpdateCountdown();
    ApplyTimerMode();
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

    // 播放结束检测 + 看门狗
    if (g_playExam >= 0)
    {
        bool playing = AudioIsPlaying();

        if (playing) g_playVerified = true;

        // 刚 open 的那一瞬间可能还没报告 playing，先给它一点时间，
        // 否则会把刚开始没多久的音频误判为“已播完”。
        // MP4 走 Media Foundation，解码器初始化更慢，宽限期放宽到 3 秒。
        ULONGLONG elapsed = GetTickCount64() - g_playStartTick;
        bool graceOver = elapsed > 3000;

        if (g_playVerified && !playing)
        {
            if (g_playExam < g_examCount &&
                g_exam[g_playExam].nodeState[g_playKind] == ST_PLAY)
            {
                g_exam[g_playExam].nodeState[g_playKind] = ST_DONE;
                Log(L"播放结束：" + g_exam[g_playExam].name + L" · " + NodeName(g_playKind));
            }
            g_playExam = -1;
            g_playKind = -1;
            UpdateAllNodeRows();
        }
        else if (!g_playVerified && graceOver && !playing)
        {
            // 一直没进入播放状态，按失败收尾，避免卡住整条队列
            Log(L"音频未能进入播放状态，已跳过");
            if (g_playExam < g_examCount &&
                g_exam[g_playExam].nodeState[g_playKind] == ST_PLAY)
            {
                g_exam[g_playExam].nodeState[g_playKind] = ST_DONE;
            }
            g_playExam = -1;
            g_playKind = -1;
            UpdateAllNodeRows();
        }
        else if (elapsed > 10u * 60u * 1000u)
        {
            // 看门狗：一段音频超过 10 分钟仍在“播放”，多半是设备被拔掉 / 驱动卡死
            Log(L"音频播放超时（超过 10 分钟），强制结束并继续后续节点");
            AudioStopAll();
            if (g_playExam < g_examCount &&
                g_exam[g_playExam].nodeState[g_playKind] == ST_PLAY)
            {
                g_exam[g_playExam].nodeState[g_playKind] = ST_DONE;
            }
            g_playExam = -1;
            g_playKind = -1;
            UpdateAllNodeRows();
        }
    }

    // 空闲出来就取下一段排队的铃声
    PumpBellQueue();

    if (!g_running)
    {
        // 空闲时也定期刷新，让“已过期”这类按时间推导的状态能跟上
        static ULONGLONG lastIdleRefresh = 0;
        ULONGLONG nowTick = GetTickCount64();
        if (nowTick - lastIdleRefresh >= 2000)
        {
            lastIdleRefresh = nowTick;
            UpdateNodePanel();
            InvalidateRect(hExamList, NULL, TRUE);
        }

        UpdateCountdown();
        return;
    }

    const __int64 tolerance = 60LL * 10000000LL;

    for (int i = 0; i < g_examCount; i++)
    {
        for (int k = 0; k < NODE_COUNT; k++)
        {
            if (g_exam[i].nodeState[k] != ST_PEND) continue;
            if (now < g_exam[i].nodeFt[k]) continue;

            if (now - g_exam[i].nodeFt[k] <= tolerance)
            {
                TriggerBell(i, k);          // 60 秒容差内立即补响
            }
            else
            {
                g_exam[i].nodeState[k] = ST_MISS;
                Log(L"已错过：" + g_exam[i].name + L" · " + NodeName(k));
                UpdateAllNodeRows();
            }
        }
    }

    UpdateCountdown();
    CheckAllFinished();
    ApplyTimerMode();
}

// 空闲时降低轮询频率（1s），监测中保持 200ms，兼顾精度与功耗
static void ApplyTimerMode()
{
    static UINT lastInterval = 0;

    UINT want = (g_running || g_playExam >= 0) ? 200 : 1000;
    if (want == lastInterval) return;

    lastInterval = want;
    SetTimer(g_hwnd, TIMER_TICK, want, NULL);
}

// ---------------------------------------------------------------------------
// 启动与急停
// ---------------------------------------------------------------------------

// 按当前时间为一场景生成三个节点的时刻与初始状态，返回待触发的节点数
static int PrepareExamNodes(int index)
{
    if (index < 0 || index >= g_examCount) return 0;

    const __int64 minute = 60LL * 10000000LL;
    const __int64 tolerance = 60LL * 10000000LL;
    __int64 now = FtNow();

    Exam& e = g_exam[index];
    int pending = 0;

    for (int k = 0; k < NODE_COUNT; k++)
    {
        e.nodeFt[k] = e.startFt + (__int64)kNodeOffsetMin[k] * minute;

        if (now - e.nodeFt[k] > tolerance)
        {
            e.nodeState[k] = ST_MISS;
        }
        else
        {
            e.nodeState[k] = ST_PEND;
            pending++;
        }
    }

    return pending;
}

// 监测期间阻止系统休眠 / 关闭显示器，避免到点不响
static void SetAwake(bool on)
{
    static bool awake = false;
    if (on == awake) return;

    awake = on;
    SetThreadExecutionState(on
                            ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
                            : ES_CONTINUOUS);
}

static void StartMonitoring()
{
    g_stoppedByUser = false;
    g_playExam = -1;
    g_playKind = -1;
    g_bellQueue.clear();
    g_playVerified = false;
    AudioStopAll();

    int activeNodes = 0;
    int firstActive = -1;

    for (int i = 0; i < g_examCount; i++)
    {
        int pending = PrepareExamNodes(i);

        if (pending == 0)
        {
            Log(L"跳过已过期：" + g_exam[i].name + L" " + FormatMonthDayHm(g_exam[i].startFt));
        }
        else
        {
            activeNodes += pending;
            if (firstActive < 0) firstActive = i;
        }
    }

    // 自动把面板切到第一场未结束的考试，避免默认停在过期场次上
    if (firstActive >= 0)
    {
        g_selectedExam = firstActive;
        SendMessageW(hExamList, LB_SETCURSEL, firstActive, 0);
    }

    g_running = true;
    SetAwake(true);

    EnableWindow(hBtnStart, FALSE);
    SetWindowTextW(hBtnStart, L"监测中");
    InvalidateRect(hBtnStart, NULL, TRUE);

    // 运行中仍可继续排入新场次，只锁定删除类操作
    EnableWindow(hBtnDel, FALSE);
    EnableWindow(hBtnClear, FALSE);

    wchar_t buf[64];
    swprintf(buf, 64, L"开始监测 · 共 %d 场，%d 个节点待触发", g_examCount, activeNodes);
    Log(buf);

    UpdateAllNodeRows();
    UpdateCountdown();
    ApplyTimerMode();
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
    AudioStopAll();
    g_playExam = -1;
    g_playKind = -1;
    g_bellQueue.clear();
    g_playVerified = false;

    for (int i = 0; i < g_examCount; i++)
    {
        for (int k = 0; k < NODE_COUNT; k++)
        {
            if (g_exam[i].nodeState[k] == ST_PEND || g_exam[i].nodeState[k] == ST_PLAY)
            {
                g_exam[i].nodeState[k] = ST_CANC;
            }
        }
    }

    g_running = false;
    g_stoppedByUser = true;

    EnableWindow(hBtnStart, TRUE);
    SetWindowTextW(hBtnStart, L"启动监测");
    InvalidateRect(hBtnStart, NULL, TRUE);

    EnableWindow(hBtnDel, g_examCount > 0);
    EnableWindow(hBtnClear, g_examCount > 0);

    Log(L"【急停】已停止播放并取消全部后续任务");

    UpdateAllNodeRows();
    UpdateCountdown();
    ApplyTimerMode();
}

// 清空输入区域，便于继续添加下一场
static void ResetInputRow()
{
    SetWindowTextW(hEditName, L"");
    SetPickTime(FtFromNowMinutes(30));
}

static void AddExamFromInput()
{
    if (g_examCount >= MAX_EXAMS)
    {
        wchar_t buf[128];
        swprintf(buf, 128, L"最多可排 %d 场考试，请先删除部分场次。", MAX_EXAMS);
        MessageBoxW(g_hwnd, buf, L"数量已满", MB_OK | MB_ICONINFORMATION);
        return;
    }

    __int64 startFt = GetPickTime();

    wchar_t nameBuf[128] = {0};
    GetWindowTextW(hEditName, nameBuf, 128);
    std::wstring name = Trim(nameBuf);

    if (startFt <= 0)
    {
        MessageBoxW(g_hwnd, L"请选择有效的考试开始时间。", L"时间无效", MB_OK | MB_ICONWARNING);
        return;
    }

    // 同一时刻已有场次时不重复添加
    for (int i = 0; i < g_examCount; i++)
    {
        if (g_exam[i].startFt == startFt)
        {
            MessageBoxW(g_hwnd, L"该开始时间的场次已存在，请调整时间。",
                        L"重复添加", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    bool inPast = (startFt <= FtNow());
    if (inPast)
    {
        if (MessageBoxW(g_hwnd,
                        (L"开始时间已过去（当前 " + FormatDateTime(FtNow()) + L"）。\r\n"
                         L"仍要添加吗？三个打铃时间可能都已过去，将记为已错过。").c_str(),
                        L"时间已过", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }
    }

    if (name.empty()) name = L"第 " + std::to_wstring(g_examCount + 1) + L" 场";

    int pos = AddExam(name, startFt);
    if (pos < 0) return;

    g_selectedExam = pos;

    if (g_running)
    {
        // 监测进行中追加：立即排好该场节点，交给 200ms 轮询统一推进
        int pending = PrepareExamNodes(pos);

        RefreshExamList();
        SaveSettings();
        ResetInputRow();
        UpdateAllNodeRows();
        UpdateCountdown();

        if (pending > 0)
        {
            Log(L"监测中追加场次：" + name + L" " + FormatMonthDayHm(startFt) +
                L"（" + std::to_wstring(pending) + L" 个节点待触发）");
        }
        else
        {
            Log(L"监测中追加场次：" + name + L" " + FormatMonthDayHm(startFt) +
                L"（打铃时间均已过去，记为已错过）");
        }
        return;
    }

    RefreshExamList();
    SaveSettings();
    ResetInputRow();
    SetFocus(hEditName);

    Log(L"已添加：" + name + L" " + FormatMonthDayHm(startFt));
}

static void DeleteSelectedExam()
{
    if (g_running)
    {
        Log(L"监测进行中，暂不能删除场次");
        return;
    }

    int sel = (int)SendMessageW(hExamList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_examCount)
    {
        MessageBoxW(g_hwnd, L"请先在日程列表中选中要删除的场次。",
                    L"未选中场次", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring name = g_exam[sel].name;
    RemoveExam(sel);

    if (g_selectedExam >= g_examCount) g_selectedExam = g_examCount - 1;
    else if (g_selectedExam > sel) g_selectedExam--;
    if (g_selectedExam < 0 && g_examCount > 0) g_selectedExam = 0;

    RefreshExamList();
    SaveSettings();
    Log(L"已删除：" + name);
}

static void ClearExams()
{
    if (g_running)
    {
        Log(L"监测进行中，暂不能清空日程");
        return;
    }

    if (g_examCount == 0) return;

    if (MessageBoxW(g_hwnd, L"确定清空全部场次吗？此操作不可撤销。",
                    L"清空日程", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    g_examCount = 0;
    g_selectedExam = -1;
    RefreshExamList();
    SaveSettings();
    Log(L"已清空全部场次");
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
    ofn.lpstrFilter = L"音频/视频文件 (*.wav;*.mp3;*.wma;*.mp4;*.m4a)\0"
                      L"*.wav;*.mp3;*.wma;*.mp4;*.m4a\0"
                      L"所有文件 (*.*)\0*.*\0";
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

    if (g_playExam >= 0 && g_playExam < g_examCount &&
        g_exam[g_playExam].nodeState[g_playKind] == ST_PLAY)
    {
        g_exam[g_playExam].nodeState[g_playKind] = ST_DONE;
        g_playExam = -1;
        g_playKind = -1;
        UpdateAllNodeRows();
    }

    if (!AudioPlay(g_audio[index]))
    {
        MessageBoxW(g_hwnd,
                    L"音频播放失败。\r\n\r\n请确认文件为 WAV / MP3 / WMA / MP4 格式且可正常播放，"
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
    for (int i = 0; i < NODE_COUNT; i++)
    {
        if (h == hNodeState[i]) return i;
    }
    return -1;
}

// 统一的胶囊：左侧小圆点 + 文字，浅底无描边（贴近任务管理类软件的状态标记）
static void DrawStatusChip(HDC dc, RECT box, int state, const wchar_t* text, HFONT font, int dotSize)
{
    HBRUSH brush = StateBrush(state);
    HGDIOBJ ob = SelectObject(dc, brush);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, box.left, box.top, box.right, box.bottom, box.bottom - box.top, box.bottom - box.top);
    SelectObject(dc, op);
    SelectObject(dc, ob);

    int cy = (box.top + box.bottom) / 2;
    int r = dotSize / 2;
    int dx = box.left + dotSize + (box.bottom - box.top) / 4;

    HBRUSH dot = CreateSolidBrush(StateColor(state));
    HGDIOBJ db = SelectObject(dc, dot);
    HGDIOBJ dp = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, dx - r, cy - r, dx - r + dotSize, cy - r + dotSize);
    SelectObject(dc, dp);
    SelectObject(dc, db);
    DeleteObject(dot);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, StateColor(state));

    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT tr = box;
    tr.left = dx + dotSize + dotSize / 2;
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
}

static void DrawNodeChip(LPDRAWITEMSTRUCT d, int kind)
{
    int state = NodeStateAt(kind);

    FillRect(d->hDC, &d->rcItem, bSurface);

    RECT r = d->rcItem;
    r.right -= S(1);
    r.bottom -= S(1);

    wchar_t text[64] = {0};
    GetWindowTextW(d->hwndItem, text, 64);

    DrawStatusChip(d->hDC, r, state, text, fState, S(7));
}

// 顶部运行状态胶囊：按运行态直接取色
static void DrawRunChip(LPDRAWITEMSTRUCT d)
{
    COLORREF bg, fg;
    switch (g_runChip)
    {
        case 1: bg = RGB(226, 240, 232); fg = RGB(24, 100, 70);  break;  // 运行中
        case 2: bg = RGB(252, 231, 206); fg = RGB(140, 76, 0);   break;  // 临近 1 分钟
        case 3: bg = RGB(226, 240, 232); fg = RGB(24, 100, 70);  break;  // 已完成
        case 4: bg = RGB(250, 228, 228); fg = RGB(158, 40, 34);  break;  // 已急停
        default: bg = RGB(238, 241, 245); fg = RGB(96, 105, 118); break; // 待启动
    }

    FillRect(d->hDC, &d->rcItem, bSurface);

    RECT r = d->rcItem;
    r.bottom -= S(1);

    HBRUSH brush = CreateSolidBrush(bg);
    HGDIOBJ ob = SelectObject(d->hDC, brush);
    HGDIOBJ op = SelectObject(d->hDC, GetStockObject(NULL_PEN));
    RoundRect(d->hDC, r.left, r.top, r.right, r.bottom,
              r.bottom - r.top, r.bottom - r.top);
    SelectObject(d->hDC, op);
    SelectObject(d->hDC, ob);
    DeleteObject(brush);

    int cy = (r.top + r.bottom) / 2;
    int dot = S(7);

    HBRUSH db = CreateSolidBrush(fg);
    HGDIOBJ b1 = SelectObject(d->hDC, db);
    HGDIOBJ p1 = SelectObject(d->hDC, GetStockObject(NULL_PEN));
    Ellipse(d->hDC, r.left + S(11), cy - dot / 2,
            r.left + S(11) + dot, cy - dot / 2 + dot);
    SelectObject(d->hDC, p1);
    SelectObject(d->hDC, b1);
    DeleteObject(db);

    wchar_t text[64] = {0};
    GetWindowTextW(d->hwndItem, text, 64);

    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, fg);

    RECT tr = r;
    tr.left += S(26);
    HGDIOBJ f = SelectObject(d->hDC, fState);
    DrawTextW(d->hDC, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, f);
}

// 日程列表自绘：左侧名称 + 开始时间，右侧状态胶囊
static void DrawExamListItem(LPDRAWITEMSTRUCT d)
{
    int index = (int)d->itemID;
    RECT r = d->rcItem;

    if (index < 0 || index >= g_examCount)
    {
        FillRect(d->hDC, &r, bSurface);
        return;
    }

    bool selected = (d->itemState & ODS_SELECTED) != 0;

    FillRect(d->hDC, &r, bSurface);

    // 选中行：圆角浅绿底
    if (selected)
    {
        RECT pill = r;
        pill.left += S(4);
        pill.right -= S(4);
        pill.top += S(2);
        pill.bottom -= S(2);

        HBRUSH brush = CreateSolidBrush(RGB(238, 246, 241));
        HGDIOBJ ob = SelectObject(d->hDC, brush);
        HGDIOBJ op = SelectObject(d->hDC, GetStockObject(NULL_PEN));
        RoundRect(d->hDC, pill.left, pill.top, pill.right, pill.bottom, S(8), S(8));
        SelectObject(d->hDC, op);
        SelectObject(d->hDC, ob);
        DeleteObject(brush);
    }

    // 底部分隔线
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(240, 242, 245));
    HGDIOBJ oldPen = SelectObject(d->hDC, pen);
    MoveToEx(d->hDC, r.left + S(12), r.bottom - 1, NULL);
    LineTo(d->hDC, r.right - S(12), r.bottom - 1);
    SelectObject(d->hDC, oldPen);
    DeleteObject(pen);

    const Exam& e = g_exam[index];

    SetBkMode(d->hDC, TRANSPARENT);

    RECT nameRect = r;
    nameRect.left += S(14);
    nameRect.right -= S(96);
    nameRect.top += S(7);
    nameRect.bottom = nameRect.top + S(20);

    HGDIOBJ oldFont = SelectObject(d->hDC, fRow);
    SetTextColor(d->hDC, C_INK);
    DrawTextW(d->hDC, e.name.c_str(), -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT timeRect = nameRect;
    timeRect.top = nameRect.bottom + S(1);
    timeRect.bottom = timeRect.top + S(18);

    SelectObject(d->hDC, fState);
    SetTextColor(d->hDC, C_MUTED);
    DrawTextW(d->hDC, FormatDateTime(e.startFt).c_str(), -1, &timeRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 右侧状态胶囊
    int state = ExamDisplayState(index);
    const wchar_t* text = ExamStatusText(index);

    RECT chip;
    chip.right = r.right - S(14);
    chip.left = chip.right - S(78);
    chip.top = r.top + (r.bottom - r.top - S(24)) / 2;
    chip.bottom = chip.top + S(24);

    DrawStatusChip(d->hDC, chip, state, text, fState, S(7));

    SelectObject(d->hDC, oldFont);
}

static void DrawOwnedButton(LPDRAWITEMSTRUCT d)
{
    BtnInfo* info = (BtnInfo*)GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA);
    if (!info) return;

    bool pressed  = (d->itemState & ODS_SELECTED) != 0;
    bool disabled = (d->itemState & ODS_DISABLED) != 0;
    bool hot      = (g_hotBtn == d->hwndItem);

    COLORREF fill;
    COLORREF txt;
    if (disabled)
    {
        if (info->kind == BTN_TOOL)
        {
            fill = RGB(248, 249, 251);
            txt = C_DISABLED;
        }
        else if (info->kind == BTN_PRIMARY)
        {
            // 主按钮被禁用代表“正在跑”，用浅绿底 + 绿字表达状态，而不是死灰
            fill = RGB(232, 242, 236);
            txt = RGB(74, 120, 100);
        }
        else
        {
            fill = RGB(250, 235, 235);
            txt = RGB(178, 122, 122);
        }
    }
    else if (pressed)
    {
        fill = info->pressed;
        txt = (info->kind == BTN_TOOL) ? C_INK : RGB(255, 255, 255);
    }
    else if (hot)
    {
        fill = info->hover;
        txt = (info->kind == BTN_TOOL) ? C_INK : RGB(255, 255, 255);
    }
    else
    {
        fill = info->normal;
        txt = (info->kind == BTN_TOOL) ? C_INK : RGB(255, 255, 255);
    }

    RECT r = d->rcItem;
    int radius = S(8);

    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ ob = SelectObject(d->hDC, brush);
    HGDIOBJ op;

    if (info->kind == BTN_TOOL)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
        op = SelectObject(d->hDC, pen);
        RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(d->hDC, op);
        DeleteObject(pen);
    }
    else
    {
        op = SelectObject(d->hDC, GetStockObject(NULL_PEN));
        RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(d->hDC, op);
    }

    SelectObject(d->hDC, ob);
    DeleteObject(brush);

    wchar_t text[160] = {0};
    GetWindowTextW(d->hwndItem, text, 160);

    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, txt);

    // 大号按钮用粗体大字，小号（如添加到日程）沿用工具栏字号
    bool big = (r.bottom - r.top) > S(40);

    HFONT font = big ? (info->kind == BTN_PRIMARY ? fStart : fStop) : fBtnTool;

    HGDIOBJ oldFont = SelectObject(d->hDC, font);
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

        Log(L"校时成功（" + result->source + L"）时延 " +
            std::to_wstring(result->roundTripMs) + L" ms");
        return;
    }

    bool keptPrevious = g_synced;

    RebaseClock();
    if (!keptPrevious) g_offsetTicks = 0;

    SetWindowTextW(hSync, keptPrevious ? L"校时失败 · 沿用上次偏移" : L"未同步 · 使用本机时间");
    g_syncTextColor = C_BADTXT;
    Log(L"校时失败：" + result->message +
        (keptPrevious ? L"，继续沿用上次校正结果" : L"，已回退使用本机时间"));
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
        header.bottom = S(kHeaderH);
        FillRect(dc, &header, bSurface);

        RECT footer = client;
        footer.top = S(kFooterY);
        FillRect(dc, &footer, bSurface);

        HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 0, S(kHeaderH) - 1, NULL);
        LineTo(dc, client.right, S(kHeaderH) - 1);
        MoveToEx(dc, 0, S(kFooterY), NULL);
        LineTo(dc, client.right, S(kFooterY));
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        // 四张卡片
        DrawCard(dc, kCardExam,  L"考试日程");
        DrawCard(dc, kCardNode,  L"打铃节点");
        DrawCard(dc, kCardAudio, L"音频文件");
        DrawCard(dc, kCardLog,   L"运行日志");

        // 输入框描边（列表与日志采用无边框的嵌入式面板）
        DrawFieldFrame(dc, hEditName);

        // 空日程占位
        if (g_examCount == 0 && hExamList)
        {
            RECT lr;
            GetWindowRect(hExamList, &lr);
            POINT tl = { lr.left, lr.top };
            POINT br = { lr.right, lr.bottom };
            ScreenToClient(hwnd, &tl);
            ScreenToClient(hwnd, &br);

            RECT box = { tl.x, tl.y, br.x, br.y };
            int midY = (box.top + box.bottom) / 2;

            RECT line1 = { box.left, midY - S(20), box.right, midY + S(2) };
            RECT line2 = { box.left, midY + S(2), box.right, midY + S(24) };

            HGDIOBJ oldFont = SelectObject(dc, fRow);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, C_MUTED);
            DrawTextW(dc, L"暂无场次", -1, &line1, DT_CENTER | DT_SINGLELINE | DT_BOTTOM);
            SelectObject(dc, oldFont);

            oldFont = SelectObject(dc, fState);
            SetTextColor(dc, RGB(150, 157, 167));
            DrawTextW(dc, L"填写考试名称与开始时间后，点「添加到日程」", -1, &line2,
                      DT_CENTER | DT_SINGLELINE | DT_TOP);
            SelectObject(dc, oldFont);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;

        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, C_SURFACE);

        if (control == hSync) SetTextColor(dc, g_syncTextColor);
        else if (control == hSubtitle || control == hDate || control == hSetHint ||
                 control == hExamCount || control == hFootHint ||
                 control == hAudioFile[0] || control == hAudioFile[1] || control == hAudioFile[2] ||
                 control == hNodeTitle)
            SetTextColor(dc, C_MUTED);
        else SetTextColor(dc, C_INK);

        return (LRESULT)bSurface;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;

        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, C_INK);

        // 日志区用更浅的底色，与正文区区分
        bool isLog = (control == hLog);
        SetBkColor(dc, isLog ? RGB(250, 251, 252) : C_SURFACE);
        return (LRESULT)(isLog ? bLog : bEdit);
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, C_INK);
        SetBkColor(dc, C_SURFACE);
        return (LRESULT)bSurface;
    }

    case WM_ERASEBKGND:
        // 双缓冲已覆盖整窗，禁止系统再刷一遍背景，减少闪烁与绘制开销
        return 1;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lParam;

        if (d->CtlType == ODT_LISTBOX)
        {
            DrawExamListItem(d);
        }
        else if (d->CtlType == ODT_STATIC)
        {
            if (d->hwndItem == hCountdown)
            {
                DrawRunChip(d);
            }
            else
            {
                int idx = StateIndexOf(d->hwndItem);
                DrawNodeChip(d, idx < 0 ? 0 : idx);
            }
        }
        else
        {
            DrawOwnedButton(d);
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == ID_START && code == BN_CLICKED)
        {
            if (g_running || g_pendingStart) return 0;

            if (g_examCount == 0)
            {
                MessageBoxW(hwnd, L"请先在「考试日程」中添加至少一场考试，再启动监测。",
                            L"没有可用的场次", MB_OK | MB_ICONINFORMATION);
                SetFocus(hEditName);
                return 0;
            }

            __int64 now = FtNow();
            const __int64 minute = 60LL * 10000000LL;
            const __int64 tolerance = 60LL * 10000000LL;

            bool anyNode = false;
            for (int i = 0; i < g_examCount && !anyNode; i++)
            {
                for (int k = 0; k < NODE_COUNT; k++)
                {
                    __int64 ft = g_exam[i].startFt + (__int64)kNodeOffsetMin[k] * minute;
                    if (now - ft <= tolerance) { anyNode = true; break; }
                }
            }

            if (!anyNode)
            {
                MessageBoxW(hwnd,
                            L"日程中所有场次的打铃时间都已过去超过 60 秒。\r\n"
                            L"请添加新的场次，或调整场次时间后再启动。",
                            L"没有可触发的节点", MB_OK | MB_ICONWARNING);
                return 0;
            }

            g_pendingStart = true;

            EnableWindow(hBtnStart, FALSE);
            SetWindowTextW(hBtnStart, L"启动中…");
            InvalidateRect(hBtnStart, NULL, TRUE);

            // 刚校时成功过（2 分钟内）则直接启动，避免对同一时间源连发请求被限流
            if (g_synced && GetTickCount64() - g_lastSyncTick < 120000)
            {
                g_pendingStart = false;
                StartMonitoring();
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

        if (id == ID_ADD_EXAM && code == BN_CLICKED)
        {
            AddExamFromInput();
            return 0;
        }

        if (id == ID_DEL_EXAM && code == BN_CLICKED)
        {
            DeleteSelectedExam();
            return 0;
        }

        if (id == ID_CLEAR_EXAM && code == BN_CLICKED)
        {
            ClearExams();
            return 0;
        }

        if (id == ID_EXAM_LIST)
        {
            if (code == LBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(hExamList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g_examCount)
                {
                    g_selectedExam = sel;
                    UpdateNodePanel();
                }
            }
            return 0;
        }

        for (int i = 0; i < NODE_COUNT; i++)
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
            StartMonitoring();
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

    case WM_APP + 21:
        // 第二次启动时被唤起：提示窗而不是默默无闻
        FlashWindow(hwnd, TRUE);
        SetForegroundWindow(hwnd);
        return 0;

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
        AudioStopAll();
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
    // 单实例：同一台机器只能有一份在跑。
    // 若允许开两份，同一个节点会被两个进程各响一次，考场会听到双响。
    // 注意用 Local\ 而非 Global\：Global 命名空间在非管理员会话下会创建失败并返回
    // NULL，那样检测就被静默跳过了。互斥量只覆盖本会话，因此再叠一层窗口查找。
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\ExamBell_SingleInstance");
    bool  mutexDup = (mutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS);

    HWND existing = FindWindowW(L"ExamBellWindow", NULL);

    if (mutexDup || existing)
    {
        if (existing)
        {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            PostMessageW(existing, WM_APP + 21, 0, 0);   // 让已有窗口闪一下，提示用户
        }

        MessageBoxW(NULL,
                    L"考试自动打铃已在运行，请勿重复打开。\n\n"
                    L"重复运行会导致同一时刻响两次铃。",
                    L"已在运行", MB_OK | MB_ICONWARNING);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();

    // 崩溃也要留证据
    SetUnhandledExceptionFilter(CrashHandler);

    HDC screen = GetDC(NULL);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    if (g_dpi <= 0) g_dpi = 96;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_DATE_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

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

    // 自绘时间选择器
    WNDCLASSEXW pc;
    ZeroMemory(&pc, sizeof(pc));
    pc.cbSize = sizeof(pc);
    pc.style = CS_HREDRAW | CS_VREDRAW;
    pc.lpfnWndProc = TimePickProc;
    pc.hInstance = instance;
    pc.hCursor = LoadCursor(NULL, IDC_ARROW);
    pc.hbrBackground = NULL;
    pc.lpszClassName = L"EBTimePick";
    RegisterClassExW(&pc);

    // 弹出月历
    WNDCLASSEXW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.cbSize = sizeof(cc);
    cc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    cc.lpfnWndProc = CalendarProc;
    cc.hInstance = instance;
    cc.hCursor = LoadCursor(NULL, IDC_ARROW);
    cc.hbrBackground = NULL;
    cc.lpszClassName = L"EBCalendar";
    RegisterClassExW(&cc);

    RECT rc = { 0, 0, S(1020), S(752) };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    HWND hwnd = CreateWindowExW(0, L"ExamBellWindow", L"考试自动打铃",
                                WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
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
    RefreshExamList();
    UpdateCountdown();

    ShowWindow(hwnd, SW_SHOWNORMAL);
    CenterWindow(hwnd);
    UpdateWindow(hwnd);

    SetTimer(hwnd, TIMER_TICK, 1000, NULL);

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
