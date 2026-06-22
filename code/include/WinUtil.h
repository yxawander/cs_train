#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdint>
#include <string>

namespace winutil {

std::wstring getLastErrorMessage(DWORD errorCode = GetLastError());
std::wstring getCurrentDirectory();
std::wstring getComputerNameSafe();
std::wstring getUserNameSafe();

bool hasWildcard(const std::wstring& path);
bool pathExists(const std::wstring& path);
bool isDirectory(const std::wstring& path);

std::wstring getFullPath(const std::wstring& path);
std::wstring directoryPart(const std::wstring& path);
std::wstring ensureTrailingBackslash(std::wstring path);
std::wstring makeDirSearchPattern(const std::wstring& target);
std::wstring getListingDirectory(const std::wstring& target);
std::wstring getVolumeRoot(const std::wstring& path);

std::wstring formatFileTime(const FILETIME& fileTime);
std::wstring formatUnsigned(std::uint64_t value);
std::wstring formatFileSize(std::uint64_t value);
std::wstring formatVolumeSerial(DWORD serial);

bool tryParsePid(const std::wstring& text, DWORD& pid);

} // namespace winutil

