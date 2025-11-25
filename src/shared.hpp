#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

// Common enums shared by exe/dlls
enum class TrayState : int
{
    Disconnected = 0,
    ConnectedIdle = 1,
    Uploading = 2,
    Downloading = 3,
    Error = 4,
    Completed = 5
};

struct TrayProgress
{
    uint64_t current{0};
    uint64_t total{0};
};

enum class ChangeType : int
{
    Add = 0,
    Update = 1,
    Delete = 2,
    Rename = 3
};

struct FileChange
{
    ChangeType type{ChangeType::Add};
    std::wstring path;
    std::wstring newPath; // for rename
    uint64_t size{0};
    std::array<unsigned char, 16> md5{};
};

struct DesktopLayout
{
    int screenW{0};
    int screenH{0};
    double dpiX{96.0};
    double dpiY{96.0};
    POINT cornerMin{0, 0};
    POINT cornerMax{0, 0};
    double gridW{0};
    double gridH{0};
    RECT cells[3][3]{};
};

struct IniState
{
    DesktopLayout layout{};
    std::wstring desktopPath;
    std::wstring targetDir;
    std::wstring iniPath;
    uint64_t lastSync{0};
    std::wstring peerIpv4;
};

struct PeerInfo
{
    uint64_t selfId{0};
    uint64_t peerId{0};
    std::string ipv4;
    uint64_t timestamp{0};
};

using TriggerCallback = void(__stdcall*)(void*);
using TrayCallback = void(__stdcall*)(TrayState, uint64_t, uint64_t, const wchar_t*, void*);