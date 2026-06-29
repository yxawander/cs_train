#include "WinUtil.h"

#include <iomanip>
#include <sstream>
#include <vector>

namespace winutil {

// 将保存下来的 GetLastError 错误码转换为可读文本。
std::wstring getLastErrorMessage(DWORD errorCode) {
    if (errorCode == 0) {
        return L"no error";
    }

    LPWSTR buffer = nullptr;
    // FORMAT_MESSAGE_ALLOCATE_BUFFER 会让系统分配 buffer，使用完必须 LocalFree。
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message;
    if (size != 0 && buffer != nullptr) {
        message.assign(buffer, size);
        LocalFree(buffer);
    } else {
        std::wostringstream oss;
        oss << L"Windows error " << errorCode;
        message = oss.str();
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.')) {
        message.pop_back();
    }
    return message;
}

// 使用动态缓冲区获取当前目录，避免固定长度路径限制。
std::wstring getCurrentDirectory() {
    DWORD required = GetCurrentDirectoryW(0, nullptr);
    if (required == 0) {
        return L".";
    }

    std::wstring buffer(required, L'\0');
    DWORD written = GetCurrentDirectoryW(required, buffer.data());
    if (written == 0) {
        return L".";
    }
    buffer.resize(written);
    return buffer;
}

// 读取 Windows 计算机名，失败时返回默认值。
std::wstring getComputerNameSafe() {
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buffer, &size)) {
        return std::wstring(buffer, size);
    }
    return L"UNKNOWN-PC";
}

// 读取 Windows 用户名，失败时返回默认值。
std::wstring getUserNameSafe() {
    wchar_t buffer[256] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    if (GetUserNameW(buffer, &size) && size > 0) {
        return std::wstring(buffer, size - 1);
    }
    return L"USER";
}

// 判断路径中是否包含 *.cpp 或 test?.txt 这类通配符。
bool hasWildcard(const std::wstring& path) {
    return path.find_first_of(L"*?") != std::wstring::npos;
}

// 根据 Win32 文件属性判断路径是否存在。
bool pathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 判断路径是否存在且是否为目录。
bool isDirectory(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// 尽量将相对路径转换为完整路径。
std::wstring getFullPath(const std::wstring& path) {
    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        return path;
    }

    std::wstring buffer(required, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), required, buffer.data(), nullptr);
    if (written == 0) {
        return path;
    }
    buffer.resize(written);
    return buffer;
}

// 从普通路径或通配符表达式中提取目录部分。
std::wstring directoryPart(const std::wstring& path) {
    if (path.empty()) {
        return L".";
    }

    std::size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }

    if (pos == 0) {
        return path.substr(0, 1);
    }

    if (pos == 2 && path.size() >= 3 && path[1] == L':') {
        return path.substr(0, 3);
    }

    return path.substr(0, pos);
}

// 确保目录路径末尾有斜杠，便于继续拼接文件名或 *。
std::wstring ensureTrailingBackslash(std::wstring path) {
    if (path.empty()) {
        return L".\\";
    }
    wchar_t last = path.back();
    if (last != L'\\' && last != L'/') {
        path.push_back(L'\\');
    }
    return path;
}

// 构造传给 FindFirstFileW 的搜索模式，供 dir/del 使用。
std::wstring makeDirSearchPattern(const std::wstring& target) {
    std::wstring effective = target.empty() ? L"." : target;
    if (!hasWildcard(effective) && isDirectory(effective)) {
        return ensureTrailingBackslash(effective) + L"*";
    }
    return effective;
}

// 获取 dir 输出中 “Directory of ...” 应显示的目录。
std::wstring getListingDirectory(const std::wstring& target) {
    std::wstring effective = target.empty() ? L"." : target;
    if (!hasWildcard(effective) && isDirectory(effective)) {
        return getFullPath(effective);
    }
    return getFullPath(directoryPart(effective));
}

// 获取路径所在卷的根目录，例如 C:\，用于查询磁盘信息。
std::wstring getVolumeRoot(const std::wstring& path) {
    std::wstring full = getFullPath(path.empty() ? L"." : path);
    std::vector<wchar_t> buffer(MAX_PATH + 16, L'\0');
    // 优先让系统根据路径判断卷根目录，兼容挂载点和普通盘符路径。
    if (GetVolumePathNameW(full.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()))) {
        return std::wstring(buffer.data());
    }

    if (full.size() >= 3 && full[1] == L':' && (full[2] == L'\\' || full[2] == L'/')) {
        return full.substr(0, 3);
    }
    return L"";
}

// 将 FILETIME 转换成本地时间字符串，格式为 yyyy/MM/dd HH:mm。
std::wstring formatFileTime(const FILETIME& fileTime) {
    FILETIME localFileTime = {};
    SYSTEMTIME systemTime = {};
    if (!FileTimeToLocalFileTime(&fileTime, &localFileTime) ||
        !FileTimeToSystemTime(&localFileTime, &systemTime)) {
        return L"----/--/-- --:--";
    }

    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(4) << systemTime.wYear << L'/'
        << std::setw(2) << systemTime.wMonth << L'/'
        << std::setw(2) << systemTime.wDay << L' '
        << std::setw(2) << systemTime.wHour << L':'
        << std::setw(2) << systemTime.wMinute;
    return oss.str();
}

// 给字节数和普通整数添加千位分隔符。
std::wstring formatUnsigned(std::uint64_t value) {
    std::wstring digits = std::to_wstring(value);
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<std::size_t>(i), 1, L',');
    }
    return digits;
}

// 格式化文件大小，目前复用普通整数格式。
std::wstring formatFileSize(std::uint64_t value) {
    return formatUnsigned(value);
}

// 将 DWORD 卷序列号格式化为 XXXX-XXXX。
std::wstring formatVolumeSerial(DWORD serial) {
    std::wostringstream oss;
    oss << std::uppercase << std::hex << std::setfill(L'0')
        << std::setw(4) << ((serial >> 16) & 0xFFFF)
        << L'-'
        << std::setw(4) << (serial & 0xFFFF);
    return oss.str();
}

// 解析十进制 PID，并拒绝非数字和溢出输入。
bool tryParsePid(const std::wstring& text, DWORD& pid) {
    if (text.empty()) {
        return false;
    }

    unsigned long long value = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        // 边累加边检查 DWORD 范围，避免非法 PID 溢出后被截断成另一个值。
        value = value * 10 + static_cast<unsigned long long>(ch - L'0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }

    pid = static_cast<DWORD>(value);
    return true;
}

} // namespace winutil
