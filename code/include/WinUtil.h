#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdint>
#include <string>

namespace winutil {

// 本命名空间只放与 Win32 调用相关的辅助函数，避免 Shell.cpp 混入太多格式化和路径细节。

// 将 Win32 错误码转换为可读的系统错误信息。
std::wstring getLastErrorMessage(DWORD errorCode = GetLastError());
// 获取当前进程的工作目录。
std::wstring getCurrentDirectory();
// 获取计算机名和用户名，失败时返回稳定的默认值。
std::wstring getComputerNameSafe();
std::wstring getUserNameSafe();

// 路径判断工具，用于目录和通配符命令。
bool hasWildcard(const std::wstring& path);
bool pathExists(const std::wstring& path);
bool isDirectory(const std::wstring& path);

// 路径处理工具，用于构造 FindFirstFileW 搜索模式和显示路径。
std::wstring getFullPath(const std::wstring& path);
std::wstring directoryPart(const std::wstring& path);
std::wstring ensureTrailingBackslash(std::wstring path);
std::wstring makeDirSearchPattern(const std::wstring& target);
std::wstring getListingDirectory(const std::wstring& target);
std::wstring getVolumeRoot(const std::wstring& path);

// dir/task 等命令使用的格式化工具。
std::wstring formatFileTime(const FILETIME& fileTime);
std::wstring formatUnsigned(std::uint64_t value);
std::wstring formatFileSize(std::uint64_t value);
std::wstring formatVolumeSerial(DWORD serial);

// 将用户输入的 PID 转成 DWORD，并检查非法字符和溢出。
bool tryParsePid(const std::wstring& text, DWORD& pid);

} // namespace winutil
