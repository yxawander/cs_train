#include "WinUtil.h"

#include <iomanip>
#include <sstream>
#include <vector>

namespace winutil {

std::wstring getLastErrorMessage(DWORD errorCode) {
    if (errorCode == 0) {
        return L"no error";
    }

    LPWSTR buffer = nullptr;
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

std::wstring getComputerNameSafe() {
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buffer, &size)) {
        return std::wstring(buffer, size);
    }
    return L"UNKNOWN-PC";
}

std::wstring getUserNameSafe() {
    wchar_t buffer[256] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    if (GetUserNameW(buffer, &size) && size > 0) {
        return std::wstring(buffer, size - 1);
    }
    return L"USER";
}

bool hasWildcard(const std::wstring& path) {
    return path.find_first_of(L"*?") != std::wstring::npos;
}

bool pathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

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

std::wstring makeDirSearchPattern(const std::wstring& target) {
    std::wstring effective = target.empty() ? L"." : target;
    if (!hasWildcard(effective) && isDirectory(effective)) {
        return ensureTrailingBackslash(effective) + L"*";
    }
    return effective;
}

std::wstring getListingDirectory(const std::wstring& target) {
    std::wstring effective = target.empty() ? L"." : target;
    if (!hasWildcard(effective) && isDirectory(effective)) {
        return getFullPath(effective);
    }
    return getFullPath(directoryPart(effective));
}

std::wstring getVolumeRoot(const std::wstring& path) {
    std::wstring full = getFullPath(path.empty() ? L"." : path);
    std::vector<wchar_t> buffer(MAX_PATH + 16, L'\0');
    if (GetVolumePathNameW(full.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()))) {
        return std::wstring(buffer.data());
    }

    if (full.size() >= 3 && full[1] == L':' && (full[2] == L'\\' || full[2] == L'/')) {
        return full.substr(0, 3);
    }
    return L"";
}

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

std::wstring formatUnsigned(std::uint64_t value) {
    std::wstring digits = std::to_wstring(value);
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<std::size_t>(i), 1, L',');
    }
    return digits;
}

std::wstring formatFileSize(std::uint64_t value) {
    return formatUnsigned(value);
}

std::wstring formatVolumeSerial(DWORD serial) {
    std::wostringstream oss;
    oss << std::uppercase << std::hex << std::setfill(L'0')
        << std::setw(4) << ((serial >> 16) & 0xFFFF)
        << L'-'
        << std::setw(4) << (serial & 0xFFFF);
    return oss.str();
}

bool tryParsePid(const std::wstring& text, DWORD& pid) {
    if (text.empty()) {
        return false;
    }

    unsigned long long value = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned long long>(ch - L'0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }

    pid = static_cast<DWORD>(value);
    return true;
}

} // namespace winutil
