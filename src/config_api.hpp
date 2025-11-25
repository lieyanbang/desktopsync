#pragma once
#include "shared.hpp"

#ifdef BUILD_CONFIG_DLL
#define CONFIG_API extern "C" __declspec(dllexport)
#else
#define CONFIG_API extern "C" __declspec(dllimport)
#endif

CONFIG_API bool LoadOrInitConfig(IniState* outState);
CONFIG_API bool SavePeerToIni(const wchar_t* iniPath, const wchar_t* ipv4);
CONFIG_API void UpdateLastSync(const wchar_t* iniPath, uint64_t ts);