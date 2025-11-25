#pragma once
#include "shared.hpp"

#ifdef BUILD_P2P_DLL
#define P2P_API extern "C" __declspec(dllexport)
#else
#define P2P_API extern "C" __declspec(dllimport)
#endif

P2P_API bool RunP2PHandshake(wchar_t* outIp, int outLen, uint64_t* outPeerId, uint64_t* outSelfId);