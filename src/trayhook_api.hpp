#pragma once
#include "shared.hpp"

#ifdef BUILD_TRAY_DLL
#define TRAY_API extern "C" __declspec(dllexport)
#else
#define TRAY_API extern "C" __declspec(dllimport)
#endif

TRAY_API bool StartTrayHook(const wchar_t* peerIp, TriggerCallback cb, void* ctx);
TRAY_API void UpdateTray(TrayState state, uint64_t cur, uint64_t total, const wchar_t* err);
TRAY_API void StopTrayHook();