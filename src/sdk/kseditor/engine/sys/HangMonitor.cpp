#include "HangMonitor.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

#include <chrono>
#include <thread>

#include "LogManager.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <objbase.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")
#endif

#ifdef _WIN32
namespace {

// Dialog control ids
constexpr int kBtnRelaunch = 1;
constexpr int kBtnExportLogs = 2;
constexpr int kBtnKeepWaiting = 3;
constexpr int kBtnTerminate = 4;
constexpr int kStaticTitle = 10;
constexpr int kStaticSub = 11;

std::atomic<HWND> s_dialogHwnd {nullptr};

struct HangDialogCtx {
    int result = 0;
};

HBRUSH s_hangBgBrush = nullptr;
HBRUSH s_hangButtonBrush = nullptr;

HBRUSH ensureBrush(HBRUSH& slot, int r, int g, int b)
{
    if (!slot)
        slot = CreateSolidBrush(RGB(r, g, b));
    return slot;
}

void exportLogsToFolder()
{
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Choose a folder to export the logs to";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;

    wchar_t path[MAX_PATH] = {};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok)
        return;

    const QString src = LogManager::instance().getLogFilePath();
    if (src.isEmpty())
        return;

    QFileInfo info(src);
    if (!info.exists())
        return;

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString dest = QString::fromWCharArray(path) + "/kseditor_" + stamp + ".log";
    if (QFile::exists(dest))
        QFile::remove(dest);
    QFile::copy(src, dest);
}

bool hangMonitorHasRecovered()
{
    return HangMonitor::instance().hasRecovered();
}

void hangMonitorRequestClose()
{
    const HWND hwnd = s_dialogHwnd.load();
    if (hwnd)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK hangDialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<HangDialogCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, ensureBrush(s_hangBgBrush, 0x1e, 0x1e, 0x1e));
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetBkColor(hdc, RGB(0x1e, 0x1e, 0x1e));
        if (id == kStaticTitle)
            SetTextColor(hdc, RGB(255, 255, 255));
        else
            SetTextColor(hdc, RGB(160, 160, 160));
        return reinterpret_cast<LRESULT>(ensureBrush(s_hangBgBrush, 0x1e, 0x1e, 0x1e));
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0x2d, 0x2d, 0x30));
        return reinterpret_cast<LRESULT>(ensureBrush(s_hangButtonBrush, 0x2d, 0x2d, 0x30));
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kBtnExportLogs) {
            exportLogsToFolder();
            return 0;
        }
        if (id == kBtnRelaunch || id == kBtnKeepWaiting || id == kBtnTerminate) {
            if (ctx)
                ctx->result = id;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_TIMER: {
        // Auto-dismiss if the app recovered while the dialog was visible.
        if (hangMonitorHasRecovered()) {
            if (ctx)
                ctx->result = kBtnKeepWaiting;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE: {
        if (ctx)
            ctx->result = kBtnKeepWaiting;
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int showNativeHangDialog()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = hangDialogWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ksEditorHangDialogWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    HangDialogCtx ctx;
    ctx.result = 0;

    // Center on the primary work area.
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ksEditorHangDialogWindow",
        L"ksEditor",
        WS_CAPTION | WS_SYSMENU,
        x, y, 400, 260,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hwnd) {
        CoUninitialize();
        return kBtnKeepWaiting;
    }

    s_dialogHwnd.store(hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));

    // Scale for DPI.
    UINT dpi = 96;
    auto* pGetDpiForWindow = reinterpret_cast<UINT (WINAPI*)(HWND)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (pGetDpiForWindow)
        dpi = pGetDpiForWindow(hwnd);
    const auto s = [dpi](int px) { return MulDiv(px, static_cast<int>(dpi), 96); };

    const int margin = s(20);
    const int titleHeight = s(26);
    const int subHeight = s(20);
    const int buttonHeight = s(36);
    const int buttonGap = s(8);
    const int width = s(400);
    const int height = s(260);

    RECT wa;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        x = wa.left + (wa.right - wa.left - width) / 2;
        y = wa.top + (wa.bottom - wa.top - height) / 2;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);

    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
        reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_WARNING)));

    HFONT titleFont = CreateFontW(-s(18), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT subFont = CreateFontW(-s(13), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT btnFont = CreateFontW(-s(13), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int btnWidth = width - 2 * margin;

    HWND title = CreateWindowExW(0, L"STATIC", L"ksEditor is not responding.",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        margin, margin, btnWidth, titleHeight,
        hwnd, reinterpret_cast<HMENU>(UINT_PTR(kStaticTitle)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);

    const int subTop = margin + titleHeight + s(2);
    HWND sub = CreateWindowExW(0, L"STATIC", L"make a choice",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        margin, subTop, btnWidth, subHeight,
        hwnd, reinterpret_cast<HMENU>(UINT_PTR(kStaticSub)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(sub, WM_SETFONT, reinterpret_cast<WPARAM>(subFont), TRUE);

    auto makeButton = [&](int id, const wchar_t* text, int top, bool defaultBtn) -> HWND {
        const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP
            | (defaultBtn ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        HWND btn = CreateWindowExW(0, L"BUTTON", text, style,
            margin, top, btnWidth, buttonHeight,
            hwnd, reinterpret_cast<HMENU>(UINT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(btnFont), TRUE);
        SetWindowTheme(btn, L"", L"");
        return btn;
    };

    int top = subTop + subHeight + s(18);
    makeButton(kBtnRelaunch, L"Relaunch", top, false);
    top += buttonHeight + buttonGap;
    makeButton(kBtnExportLogs, L"Export logs", top, false);
    top += buttonHeight + buttonGap;
    makeButton(kBtnKeepWaiting, L"Keep waiting", top, true);
    top += buttonHeight + buttonGap;
    makeButton(kBtnTerminate, L"Terminate program", top, false);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetTimer(hwnd, 1, 500, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(titleFont);
    DeleteObject(subFont);
    DeleteObject(btnFont);

    s_dialogHwnd.store(nullptr);
    CoUninitialize();
    return ctx.result;
}

void relaunchApplication()
{
    wchar_t cmdLine[32768];
    wchar_t* src = GetCommandLineW();
    if (!src) {
        TerminateProcess(GetCurrentProcess(), 1);
        return;
    }
    wcsncpy_s(cmdLine, src, _TRUNCATE);
    cmdLine[32767] = L'\0';

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        // The current instance is hung; let the relaunched process take over.
        TerminateProcess(GetCurrentProcess(), 0);
    } else {
        MessageBoxW(nullptr, L"ksEditor could not be relaunched.", L"ksEditor",
            MB_OK | MB_ICONERROR);
    }
}

} // namespace
#endif // _WIN32

HangMonitor& HangMonitor::instance()
{
    static HangMonitor inst;
    return inst;
}

HangMonitor::~HangMonitor()
{
    stop();
}

void HangMonitor::start()
{
    if (m_running.load())
        return;

    m_lastHeartbeatMs.store(QDateTime::currentMSecsSinceEpoch());

    if (!m_heartbeatTimer) {
        m_heartbeatTimer = new QTimer();
        m_heartbeatTimer->setInterval(static_cast<int>(kHeartbeatIntervalMs));
        QObject::connect(m_heartbeatTimer, &QTimer::timeout, [this]() {
            m_lastHeartbeatMs.store(QDateTime::currentMSecsSinceEpoch());
        });
    }
    m_heartbeatTimer->start();

    m_running.store(true);
    m_watchdog = std::thread(&HangMonitor::watchdogLoop, this);
}

void HangMonitor::stop()
{
    if (!m_running.exchange(false))
        return;

#ifdef _WIN32
    hangMonitorRequestClose();
#endif

    if (m_watchdog.joinable())
        m_watchdog.join();

    if (m_heartbeatTimer) {
        if (QCoreApplication::instance())
            delete m_heartbeatTimer;
        m_heartbeatTimer = nullptr;
    }
}

bool HangMonitor::hasRecovered() const
{
    return (QDateTime::currentMSecsSinceEpoch() - m_lastHeartbeatMs.load())
        < kHangThresholdMs;
}

void HangMonitor::watchdogLoop()
{
    bool coolingDown = false;
    qint64 cooldownUntil = 0;

    while (m_running.load(std::memory_order_relaxed)) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 lastHeartbeat = m_lastHeartbeatMs.load(std::memory_order_relaxed);

        if (now - lastHeartbeat > kHangThresholdMs) {
            if (!coolingDown || now >= cooldownUntil) {
                coolingDown = false;
                showHangDialog();
                cooldownUntil = QDateTime::currentMSecsSinceEpoch() + kWaitCooldownMs;
                coolingDown = true;
            }
        } else {
            coolingDown = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void HangMonitor::showHangDialog()
{
#ifdef _WIN32
    const int choice = showNativeHangDialog();

    if (choice == kBtnRelaunch) {
        relaunchApplication();
    } else if (choice == kBtnTerminate) {
        TerminateProcess(GetCurrentProcess(), 1);
    }
    // Every other result (Keep waiting, closed, export only) resumes watching.
#else
    // Not implemented on non-Windows platforms.
#endif
}
