#define BUILD_TRAY_DLL
#define _WINSOCKAPI_
#include "shared.hpp"
#include "common.hpp"
#include <shellapi.h>
#include <shlwapi.h>
#include <atomic>
#include <thread>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

namespace
{
    constexpr UINT WM_TRAYICON = WM_APP + 1;
    constexpr UINT WM_UPDATE_STATE = WM_APP + 2;

    HINSTANCE g_hInst = nullptr;
    HWND g_hwnd = nullptr;
    NOTIFYICONDATAW g_nid{};
    HHOOK g_mouseHook = nullptr;
    TriggerCallback g_trigger = nullptr;
    void* g_triggerCtx = nullptr;
    std::wstring g_peerIp;
    TrayState g_state = TrayState::Disconnected;
    uint64_t g_cur = 0, g_total = 0;
    std::wstring g_error;
    CRITICAL_SECTION g_cs;
    HANDLE g_thread = nullptr;
    std::atomic<bool> g_running{ false };

    std::wstring IconPath(const wchar_t* name)
    {
        return GetExeDir() + L"\\icons\\" + name;
    }

    HICON LoadIco(const wchar_t* file)
    {
        std::wstring path = IconPath(file);
        return static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }

    void ApplyTray()
    {
        EnterCriticalSection(&g_cs);
        TrayState st = g_state; uint64_t c = g_cur, t = g_total; std::wstring err = g_error; std::wstring peer = g_peerIp;
        LeaveCriticalSection(&g_cs);

        const wchar_t* iconName = L"CloudOff.ico";
        std::wstring tip;
        switch (st)
        {
        case TrayState::Disconnected:
            tip = L"Not connected";
            iconName = L"CloudOff.ico";
            break;
        case TrayState::ConnectedIdle:
            tip = std::wstring(L"Connected: ") + peer;
            iconName = L"Cloud.ico";
            break;
        case TrayState::Uploading:
            tip = std::wstring(L"Uploading ") + Utf8ToWide(FormatBytes(c)) + L"/" + Utf8ToWide(FormatBytes(t));
            iconName = L"Cloud.ico";
            break;
        case TrayState::Downloading:
            tip = std::wstring(L"Downloading ") + Utf8ToWide(FormatBytes(c)) + L"/" + Utf8ToWide(FormatBytes(t));
            iconName = L"CloudDownload.ico";
            break;
        case TrayState::Error:
            tip = std::wstring(L"Error: ") + err;
            iconName = L"CloudAlert.ico";
            break;
        case TrayState::Completed:
            tip = L"Done";
            iconName = L"CloudDone.ico";
            break;
        }

        HICON ico = LoadIco(iconName);
        if (ico)
        {
            g_nid.hIcon = ico;
            wcscpy_s(g_nid.szTip, tip.c_str());
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            DestroyIcon(ico);
        }
    }

    bool IsDesktopListView(HWND hwnd)
    {
        if (!hwnd) return false;
        wchar_t cls[64]{};
        HWND cur = hwnd;
        while (cur)
        {
            GetClassNameW(cur, cls, 64);
            if (wcscmp(cls, L"SysListView32") == 0)
            {
                HWND parent = GetParent(cur);
                wchar_t parentCls[64]{};
                GetClassNameW(parent, parentCls, 64);
                if (wcscmp(parentCls, L"SHELLDLL_DefView") == 0)
                {
                    return true;
                }
            }
            cur = GetParent(cur);
        }
        return false;
    }

    LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN)
        {
            const MSLLHOOKSTRUCT* p = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            POINT pt = p->pt;
            HWND h = WindowFromPoint(pt);
            if (IsDesktopListView(h))
            {
                if (g_trigger) g_trigger(g_triggerCtx);
            }
        }
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            return 0;
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP)
            {
                HMENU menu = CreatePopupMenu();
                EnterCriticalSection(&g_cs);
                std::wstring label = g_peerIp.empty() ? L"Resend broadcast" : g_peerIp;
                LeaveCriticalSection(&g_cs);
                AppendMenuW(menu, MF_STRING, 1, label.c_str());
                AppendMenuW(menu, MF_STRING, 2, L"Exit");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 2)
                {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                else if (cmd == 1)
                {
                    if (g_trigger) g_trigger(g_triggerCtx);
                }
            }
            return 0;
        case WM_UPDATE_STATE:
            ApplyTray();
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    DWORD WINAPI UiThread(LPVOID)
    {
        g_hInst = GetModuleHandleW(nullptr);
        const wchar_t* clsName = L"DesktopSyncTrayWnd";
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = clsName;
        RegisterClassExW(&wc);
        g_hwnd = CreateWindowExW(0, clsName, L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, g_hInst, nullptr);

        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = g_hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        HICON ico = LoadIco(L"CloudOff.ico");
        g_nid.hIcon = ico;
        wcscpy_s(g_nid.szTip, L"Not connected");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        if (ico) DestroyIcon(ico);

        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
        return 0;
    }
}

extern "C" __declspec(dllexport) bool StartTrayHook(const wchar_t* peerIp, TriggerCallback cb, void* ctx)
{
    if (g_running.load()) return true;
    InitializeCriticalSection(&g_cs);
    EnterCriticalSection(&g_cs);
    g_peerIp = peerIp ? peerIp : L"";
    g_state = TrayState::Disconnected;
    g_error.clear();
    g_cur = g_total = 0;
    LeaveCriticalSection(&g_cs);
    g_trigger = cb;
    g_triggerCtx = ctx;
    g_running.store(true);
    g_thread = CreateThread(nullptr, 0, UiThread, nullptr, 0, nullptr);
    return g_thread != nullptr;
}

extern "C" __declspec(dllexport) void UpdateTray(TrayState state, uint64_t cur, uint64_t total, const wchar_t* err)
{
    if (!g_running.load()) return;
    EnterCriticalSection(&g_cs);
    g_state = state;
    g_cur = cur;
    g_total = total;
    g_error = err ? err : L"";
    LeaveCriticalSection(&g_cs);
    if (g_hwnd) PostMessageW(g_hwnd, WM_UPDATE_STATE, 0, 0);
}

extern "C" __declspec(dllexport) void StopTrayHook()
{
    if (!g_running.load()) return;
    g_running.store(false);
    if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    if (g_thread)
    {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
    }
    g_thread = nullptr;
    DeleteCriticalSection(&g_cs);
}
