#include "shared.hpp"
#include <shlobj.h>
#include <shlwapi.h>
#include <chrono>
#include <filesystem>
#include <sstream>

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

uint64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t NowSec()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path().wstring();
}

std::wstring GetDesktopPath()
{
    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, path)))
    {
        return std::wstring(path);
    }
    return L"";
}

bool EnsureDirectory(const std::wstring& dir)
{
    if (dir.empty()) return false;
    if (std::filesystem::exists(dir)) return true;
    return std::filesystem::create_directories(dir);
}

std::string FormatBytes(uint64_t bytes)
{
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    size_t idx = 0;
    while (v >= 1024.0 && idx < std::size(units) - 1)
    {
        v /= 1024.0;
        ++idx;
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision((idx == 0) ? 0 : 1);
    oss << v << " " << units[idx];
    return oss.str();
}