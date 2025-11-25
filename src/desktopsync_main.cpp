#define _WINSOCKAPI_
#include "shared.hpp"
#include "common.hpp"
#include "config_api.hpp"
#include "p2p_api.hpp"
#include "trayhook_api.hpp"
#include "sync_api.hpp"
#include <windows.h>
#include <iostream>
#include <atomic>

static HANDLE g_triggerEvent = nullptr;

void __stdcall TriggerFire(void* ctx)
{
    HANDLE h = reinterpret_cast<HANDLE>(ctx);
    if (h) SetEvent(h);
}

void __stdcall TrayForward(TrayState st, uint64_t cur, uint64_t total, const wchar_t* err, void*)
{
    UpdateTray(st, cur, total, err);
}

int wmain()
{
    IniState cfg{};
    if (!LoadOrInitConfig(&cfg))
    {
        std::wcerr << L"LoadOrInitConfig failed" << std::endl;
        return 1;
    }

    wchar_t peerIp[64]{};
    uint64_t peerId = 0, selfId = 0;
    std::wcout << L"等待 P2P 握手..." << std::endl;
    if (!RunP2PHandshake(peerIp, 64, &peerId, &selfId))
    {
        std::wcerr << L"P2P 握手失败" << std::endl;
        return 1;
    }
    SavePeerToIni(cfg.iniPath.c_str(), peerIp);
    cfg.peerIpv4 = peerIp;

    std::wcout << L"对方 IP: " << peerIp << std::endl;

    g_triggerEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    StartTrayHook(peerIp, TriggerFire, g_triggerEvent);
    UpdateTray(TrayState::ConnectedIdle, 0, 0, L"");

    StartSyncService(&cfg, TrayForward, nullptr);

    for (;;)
    {
        DWORD w = WaitForSingleObject(g_triggerEvent, INFINITE);
        if (w != WAIT_OBJECT_0) break;
        ResetEvent(g_triggerEvent);
        std::wcout << L"触发同步..." << std::endl;
        bool ok = SyncUploadOnce(&cfg, peerIp, TrayForward, nullptr);
        cfg.lastSync = NowSec();
        UpdateLastSync(cfg.iniPath.c_str(), cfg.lastSync);
        if (!ok)
        {
            UpdateTray(TrayState::Error, 0, 0, L"同步失败");
        }
        else
        {
            UpdateTray(TrayState::Completed, 0, 0, L"");
            Sleep(5000);
            UpdateTray(TrayState::ConnectedIdle, 0, 0, L"");
        }
    }

    StopTrayHook();
    return 0;
}