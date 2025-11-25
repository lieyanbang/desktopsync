#define BUILD_CONFIG_DLL
#define _WINSOCKAPI_
#include "shared.hpp"
#include "common.hpp"
#include <shlwapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

static int ReadInt(const std::wstring& ini, const wchar_t* section, const wchar_t* key, int defVal)
{
    wchar_t buf[64]{};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini.c_str());
    if (buf[0] == L'\0') return defVal;
    return _wtoi(buf);
}

static uint64_t ReadUInt64(const std::wstring& ini, const wchar_t* section, const wchar_t* key, uint64_t defVal)
{
    wchar_t buf[64]{};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini.c_str());
    if (buf[0] == L'\0') return defVal;
    unsigned long long v = 0;
    if (swscanf_s(buf, L"%llu", &v) == 1) return static_cast<uint64_t>(v);
    return defVal;
}

static double ReadDouble(const std::wstring& ini, const wchar_t* section, const wchar_t* key, double defVal)
{
    wchar_t buf[64]{};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini.c_str());
    if (buf[0] == L'\0') return defVal;
    double v = 0.0;
    if (swscanf_s(buf, L"%lf", &v) == 1) return v;
    return defVal;
}

static void WriteInt(const std::wstring& ini, const wchar_t* section, const wchar_t* key, int v)
{
    wchar_t buf[64];
    swprintf_s(buf, L"%d", v);
    WritePrivateProfileStringW(section, key, buf, ini.c_str());
}

static void WriteUInt64(const std::wstring& ini, const wchar_t* section, const wchar_t* key, uint64_t v)
{
    wchar_t buf[64];
    swprintf_s(buf, L"%llu", static_cast<unsigned long long>(v));
    WritePrivateProfileStringW(section, key, buf, ini.c_str());
}

static void WriteDouble(const std::wstring& ini, const wchar_t* section, const wchar_t* key, double v)
{
    wchar_t buf[64];
    swprintf_s(buf, L"%.6f", v);
    WritePrivateProfileStringW(section, key, buf, ini.c_str());
}

static void FillCellsFromRect(DesktopLayout& layout)
{
    POINT start{ layout.cornerMin.x, layout.cornerMin.y };
    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            RECT r{};
            r.left = static_cast<LONG>(start.x + i * layout.gridW);
            r.top = static_cast<LONG>(start.y + j * layout.gridH);
            r.right = r.left + static_cast<LONG>(layout.gridW);
            r.bottom = r.top + static_cast<LONG>(layout.gridH);
            layout.cells[j][i] = r;
        }
    }
}

static bool TryLoadLayoutTxt(const std::wstring& path, DesktopLayout& layout)
{
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string key;
    while (ifs >> key)
    {
        if (key == "max_icon")
        {
            ifs >> layout.screenW >> layout.screenH;
        }
        else if (key == "corner_min")
        {
            ifs >> layout.cornerMin.x >> layout.cornerMin.y;
        }
        else if (key == "corner_max")
        {
            ifs >> layout.cornerMax.x >> layout.cornerMax.y;
        }
        else if (key == "grid_cell")
        {
            ifs >> layout.gridW >> layout.gridH;
        }
        else if (key == "cell")
        {
            int i, j, x, y;
            ifs >> i >> j >> x >> y;
            if (i >= 0 && i < 3 && j >= 0 && j < 3)
            {
                RECT r{};
                r.left = x;
                r.top = y;
                r.right = x + static_cast<int>(layout.gridW);
                r.bottom = y + static_cast<int>(layout.gridH);
                layout.cells[j][i] = r;
            }
        }
    }
    return layout.screenW > 0 && layout.screenH > 0;
}

static void ComputeDefaultLayout(DesktopLayout& layout)
{
    layout.screenW = GetSystemMetrics(SM_CXSCREEN);
    layout.screenH = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc = GetDC(nullptr);
    layout.dpiX = static_cast<double>(GetDeviceCaps(hdc, LOGPIXELSX));
    layout.dpiY = static_cast<double>(GetDeviceCaps(hdc, LOGPIXELSY));
    ReleaseDC(nullptr, hdc);

    layout.gridW = layout.screenW / 15.0; // coarse grid
    layout.gridH = layout.screenH / 12.0;
    layout.cornerMax = { layout.screenW - 10, layout.screenH - 10 };
    layout.cornerMin = { static_cast<LONG>(layout.cornerMax.x - layout.gridW * 2), static_cast<LONG>(layout.cornerMax.y - layout.gridH * 2) };
    FillCellsFromRect(layout);
}

static bool LoadFromIni(const std::wstring& ini, IniState& state)
{
    state.layout.screenW = ReadInt(ini, L"Display", L"ResolutionWidth", 0);
    state.layout.screenH = ReadInt(ini, L"Display", L"ResolutionHeight", 0);
    state.layout.dpiX = ReadDouble(ini, L"Display", L"DPI", 0.0);
    state.layout.dpiY = state.layout.dpiX;

    state.layout.cornerMin.x = ReadInt(ini, L"Corner", L"min_x", 0);
    state.layout.cornerMin.y = ReadInt(ini, L"Corner", L"min_y", 0);
    state.layout.cornerMax.x = ReadInt(ini, L"Corner", L"max_x", 0);
    state.layout.cornerMax.y = ReadInt(ini, L"Corner", L"max_y", 0);

    state.layout.gridW = ReadDouble(ini, L"Grid", L"cell_width", 0.0);
    state.layout.gridH = ReadDouble(ini, L"Grid", L"cell_height", 0.0);

    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            std::wstring kx = L"cell_" + std::to_wstring(i) + L"_" + std::to_wstring(j) + L"_x";
            std::wstring ky = L"cell_" + std::to_wstring(i) + L"_" + std::to_wstring(j) + L"_y";
            int x = ReadInt(ini, L"Cells", kx.c_str(), 0);
            int y = ReadInt(ini, L"Cells", ky.c_str(), 0);
            RECT r{};
            r.left = x;
            r.top = y;
            r.right = x + static_cast<int>(state.layout.gridW);
            r.bottom = y + static_cast<int>(state.layout.gridH);
            state.layout.cells[j][i] = r;
        }
    }

    state.desktopPath = GetDesktopPath();
    state.iniPath = ini;
    state.peerIpv4.clear();
    {
        wchar_t buf[64]{};
        GetPrivateProfileStringW(L"p2pip", L"ipv4", L"", buf, 64, ini.c_str());
        state.peerIpv4.assign(buf);
    }
    state.lastSync = ReadUInt64(ini, L"State", L"last_sync", 0);

    return state.layout.screenW > 0 && state.layout.screenH > 0 && state.layout.gridW > 0.1 && state.layout.gridH > 0.1;
}

static void SaveIni(const std::wstring& ini, const IniState& state)
{
    WriteInt(ini, L"Display", L"ResolutionWidth", state.layout.screenW);
    WriteInt(ini, L"Display", L"ResolutionHeight", state.layout.screenH);
    WriteDouble(ini, L"Display", L"DPI", state.layout.dpiX);

    WriteInt(ini, L"Corner", L"min_x", state.layout.cornerMin.x);
    WriteInt(ini, L"Corner", L"min_y", state.layout.cornerMin.y);
    WriteInt(ini, L"Corner", L"max_x", state.layout.cornerMax.x);
    WriteInt(ini, L"Corner", L"max_y", state.layout.cornerMax.y);

    WriteDouble(ini, L"Grid", L"cell_width", state.layout.gridW);
    WriteDouble(ini, L"Grid", L"cell_height", state.layout.gridH);

    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            std::wstring kx = L"cell_" + std::to_wstring(i) + L"_" + std::to_wstring(j) + L"_x";
            std::wstring ky = L"cell_" + std::to_wstring(i) + L"_" + std::to_wstring(j) + L"_y";
            WriteInt(ini, L"Cells", kx.c_str(), state.layout.cells[j][i].left);
            WriteInt(ini, L"Cells", ky.c_str(), state.layout.cells[j][i].top);
        }
    }

    if (!state.peerIpv4.empty())
    {
        WritePrivateProfileStringW(L"p2pip", L"ipv4", state.peerIpv4.c_str(), ini.c_str());
    }
    WriteUInt64(ini, L"State", L"last_sync", state.lastSync);
}

static bool IsIncomplete(const IniState& state)
{
    if (state.layout.screenW <= 0 || state.layout.screenH <= 0) return true;
    if (state.layout.gridW < 1 || state.layout.gridH < 1) return true;
    int curW = GetSystemMetrics(SM_CXSCREEN);
    int curH = GetSystemMetrics(SM_CYSCREEN);
    if (curW != state.layout.screenW || curH != state.layout.screenH) return true;
    HDC hdc = GetDC(nullptr);
    double dpi = static_cast<double>(GetDeviceCaps(hdc, LOGPIXELSX));
    ReleaseDC(nullptr, hdc);
    if (state.layout.dpiX > 0.1 && fabs(state.layout.dpiX - dpi) > 0.5) return true;
    return false;
}

extern "C" __declspec(dllexport) bool LoadOrInitConfig(IniState* outState)
{
    if (!outState) return false;
    IniState state{};
    state.desktopPath = GetDesktopPath();
    state.targetDir = state.desktopPath;
    state.iniPath = GetExeDir() + L"\\desktopsync.ini";

    bool loaded = LoadFromIni(state.iniPath, state);
    if (!loaded || IsIncomplete(state))
    {
        DesktopLayout layout{};
        std::wstring layoutTxt = GetExeDir() + L"\\desktop_layout.txt";
        if (!TryLoadLayoutTxt(layoutTxt, layout))
        {
            ComputeDefaultLayout(layout);
        }
        state.layout = layout;
        state.lastSync = 0;
        SaveIni(state.iniPath, state);
    }
    *outState = state;
    return true;
}

extern "C" __declspec(dllexport) bool SavePeerToIni(const wchar_t* iniPath, const wchar_t* ipv4)
{
    if (!iniPath || !ipv4) return false;
    return WritePrivateProfileStringW(L"p2pip", L"ipv4", ipv4, iniPath) != FALSE;
}

extern "C" __declspec(dllexport) void UpdateLastSync(const wchar_t* iniPath, uint64_t ts)
{
    if (!iniPath) return;
    WriteUInt64(iniPath, L"State", L"last_sync", ts);
}