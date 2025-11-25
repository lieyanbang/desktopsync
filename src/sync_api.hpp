#pragma once
#include "shared.hpp"

#ifdef BUILD_SYNC_DLL
#define SYNC_API extern "C" __declspec(dllexport)
#else
#define SYNC_API extern "C" __declspec(dllimport)
#endif

SYNC_API bool StartSyncService(const IniState* cfg, TrayCallback cb, void* ctx);
SYNC_API bool SyncUploadOnce(const IniState* cfg, const wchar_t* peerIp, TrayCallback cb, void* ctx);