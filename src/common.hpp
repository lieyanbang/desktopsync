#pragma once
#include "shared.hpp"
#include <string>

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& ws);
uint64_t NowMs();
uint64_t NowSec();
std::wstring GetExeDir();
std::wstring GetDesktopPath();
bool EnsureDirectory(const std::wstring& dir);
std::string FormatBytes(uint64_t bytes);