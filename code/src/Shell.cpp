#include "Shell.h"

#include "WinUtil.h"

#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Shell.cpp 中这些匿名命名空间函数只服务本文件，避免暴露到头文件形成额外接口。

// 将 WIN32_FIND_DATAW 中高低两段文件大小合成为 64 位大小。
// WIN32_FIND_DATAW 是一个文件信息结构体，用两个 32 位整数表示一个 64 位文件大小。
std::uint64_t fileSizeOf(const WIN32_FIND_DATAW& data) {
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
           static_cast<std::uint64_t>(data.nFileSizeLow);
}

// 忽略大小写比较两个宽字符串。
bool equalsIgnoreCase(const std::wstring& left, const std::wstring& right) {
    return CommandParser::toLower(left) == CommandParser::toLower(right);
}

// 忽略大小写判断文本中是否包含关键字，用于 tasklist 过滤。
bool containsIgnoreCase(const std::wstring& text, const std::wstring& keyword) {
    return CommandParser::toLower(text).find(CommandParser::toLower(keyword)) != std::wstring::npos;
}

// 解析无符号整数，供 history n 这类参数使用。
bool parseUnsigned(const std::wstring& text, std::size_t& value) {
    if (text.empty()) {
        return false;
    }

    std::size_t result = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        std::size_t digit = static_cast<std::size_t>(ch - L'0');
        // 先判断再乘 10，避免用户输入超大数字时 size_t 发生无符号溢出。
        if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }

    value = result;
    return true;
}

// 读取环境变量值，并通过 found 区分“空值”和“不存在”。
std::wstring getEnvironmentVariableValue(const std::wstring& name, bool& found) {
    found = false;
    // 询问缓冲区大小。如果变量存在且有内容，Windows 会返回需要的字符数。
    DWORD required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required == 0) {
        return L"";
    }

    // 分配缓冲区，读取环境变量值。如果读取失败或缓冲区不足，返回空字符串。
    std::wstring buffer(required, L'\0');
    DWORD written = GetEnvironmentVariableW(name.c_str(), buffer.data(), required);
    if (written == 0 || written >= required) {
        return L"";
    }

    // Windows 返回的字符串不包含终止空字符，调整缓冲区大小后返回。
    buffer.resize(written);
    found = true;
    return buffer;
}

// 展开 echo 文本中的 %ERRORLEVEL%、%CD% 和普通环境变量。
std::wstring expandEnvironmentReferences(const std::wstring& text, int lastExitCode) {
    std::wstring result;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != L'%') {
            // 普通字符不需要展开，直接复制到结果。
            result.push_back(text[i++]);
            continue;
        }

        // 发现 '%' 后继续找下一个 '%'，中间内容才被当作变量名。
        std::size_t end = text.find(L'%', i + 1);
        if (end == std::wstring::npos) {
            // 单独一个 '%' 不是合法变量引用，按原字符保留。
            result.push_back(text[i++]);
            continue;
        }

        std::wstring name = text.substr(i + 1, end - i - 1);
        if (name.empty()) {
            result += L"%%";
            i = end + 1;
            continue;
        }

        if (equalsIgnoreCase(name, L"ERRORLEVEL")) {
            // ERRORLEVEL 是 Shell 自己维护的上一条命令退出码，不来自系统环境变量。
            result += std::to_wstring(lastExitCode);
        } else if (equalsIgnoreCase(name, L"CD")) {
            // CD 是当前工作目录，同样作为特殊变量处理。
            result += winutil::getCurrentDirectory();
        } else {
            bool found = false;
            std::wstring value = getEnvironmentVariableValue(name, found);
            if (found) {
                result += value;
            } else {
                result += L"%" + name + L"%";
            }
        }
        i = end + 1;
    }
    return result;
}

// 将读取到的 UTF-8 文件字节转换为宽字符文本，兼容 UTF-8 BOM。
std::wstring bytesToText(const std::vector<char>& bytes) {
    if (bytes.empty()) {
        return L"";
    }

    std::size_t offset = 0;
    // UTF-8 BOM 是 EF BB BF，只用于标记编码，不应作为正文输出。
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        offset = 3;
    }

    int inputSize = static_cast<int>(bytes.size() - offset);
    // 第一次调用 MultiByteToWideChar 只询问转换后需要多少个 wchar_t。
    int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes.data() + offset,
        inputSize,
        nullptr,
        0);
    if (required == 0) {
        return L"";
    }

    std::wstring text(required, L'\0');
    // 第二次调用才真正把 UTF-8 字节转换为 Windows 宽字符文本。
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes.data() + offset,
        inputSize,
        text.data(),
        required);
    return text;
}

// 使用 CreateFileW/ReadFile 读取文件字节，并限制超大文件输出。
bool readFileBytes(const std::wstring& path, std::vector<char>& bytes, std::wstring& error) {
    // CreateFileW 这里以只读方式打开已有文件；W 版本支持中文路径。
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        error = winutil::getLastErrorMessage(errorCode);
        return false;
    }

    LARGE_INTEGER fileSize = {};
    // GetFileSizeEx 返回 64 位文件大小，适合处理超过 4GB 的文件元信息。
    if (!GetFileSizeEx(file, &fileSize)) {
        DWORD errorCode = GetLastError();
        error = winutil::getLastErrorMessage(errorCode);
        CloseHandle(file);
        return false;
    }
    // type 命令直接把内容输出到控制台，限制 32MB 防止误显示大文件。
    if (fileSize.QuadPart > 32ll * 1024ll * 1024ll) {
        error = L"file is too large to display";
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(fileSize.QuadPart));
    DWORD totalRead = 0;
    while (totalRead < bytes.size()) {
        DWORD chunkRead = 0;
        // 剩余未读字节数 和 64KB 之间取较小值
        DWORD toRead = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - totalRead, 64 * 1024));
        // ReadFile 参数含义：
        // file：文件句柄。
        // bytes.data() + totalRead：本轮读取的数据写入缓冲区的位置。
        // toRead：希望本轮最多读取多少字节。
        // &chunkRead：系统实际读取到的字节数。
        // nullptr：使用同步读取，不使用 OVERLAPPED 异步结构。
        if (!ReadFile(file, bytes.data() + totalRead, toRead, &chunkRead, nullptr)) {
            DWORD errorCode = GetLastError();
            error = winutil::getLastErrorMessage(errorCode);
            CloseHandle(file);
            return false;
        }
        if (chunkRead == 0) {
            break;
        }
        totalRead += chunkRead;
    }
    bytes.resize(totalRead);
    // 所有成功和失败路径都要关闭句柄，否则会造成系统资源泄漏。
    CloseHandle(file);
    return true;
}

} // namespace

// Shell 主循环：显示提示符、读取输入、执行命令。
int Shell::run() {
    while (running_) {
        printPrompt();

        std::wstring line;
        // std::getline 读取一整行命令；遇到 Ctrl+Z/输入流结束时退出循环。
        if (!std::getline(std::wcin, line)) {
            std::wcout << L"\n";
            break;
        }

        processLine(line);
    }

    return lastExitCode_;
}

// 处理一行用户输入，解析后记录历史并执行。
bool Shell::processLine(const std::wstring& line) {
    ParsedCommand command = CommandParser::parse(line);
    if (command.empty) {
        // 空行不记录历史，也不改变上一条命令退出码。
        return running_;
    }

    history_.push_back(command.original);

    // 内部命令会直接改变当前 Shell 状态；未识别的命令才交给外部进程处理。
    if (!dispatchBuiltin(command)) {
        executeExternal(command);
    }

    return running_;
}

// 输出当前目录提示符。
void Shell::printPrompt() const {
    std::wcout << winutil::getCurrentDirectory() << L"> ";
    // 立即刷新，避免提示符停留在缓冲区里不显示。
    std::wcout.flush();
}

// 根据命令名调用对应内部命令；非内部命令返回 false。
bool Shell::dispatchBuiltin(const ParsedCommand& command) {
    // command.name 已在解析阶段转成小写，所以这里可以直接比较小写命令名。
    if (command.name == L"help" || command.name == L"?") {
        cmdHelp(command);
    } else if (command.name == L"cd" || command.name == L"chdir") {
        cmdCd(command);
    } else if (command.name == L"dir") {
        cmdDir(command);
    } else if (command.name == L"history") {
        cmdHistory(command);
    } else if (command.name == L"exit") {
        cmdExit(command);
    } else if (command.name == L"tasklist") {
        cmdTasklist(command);
    } else if (command.name == L"taskkill") {
        cmdTaskkill(command);
    } else if (command.name == L"echo") {
        cmdEcho(command);
    } else if (command.name == L"pwd") {
        cmdPwd();
    } else if (command.name == L"mkdir" || command.name == L"md") {
        cmdMkdir(command);
    } else if (command.name == L"rmdir" || command.name == L"rd") {
        cmdRmdir(command);
    } else if (command.name == L"del" || command.name == L"erase") {
        cmdDel(command);
    } else if (command.name == L"copy") {
        cmdCopy(command);
    } else if (command.name == L"move" || command.name == L"ren" || command.name == L"rename") {
        cmdMove(command);
    } else if (command.name == L"type") {
        cmdType(command);
    } else if (command.name == L"set") {
        cmdSet(command);
    } else if (command.name == L"date") {
        cmdDate();
    } else if (command.name == L"time") {
        cmdTime();
    } else if (command.name == L"ver") {
        cmdVer();
    } else if (command.name == L"cls" || command.name == L"clear") {
        cmdClear();
    } else {
        return false;
    }

    return true;
}

// 显示总体帮助或单个命令的简要帮助。
void Shell::cmdHelp(const ParsedCommand& command) {
    if (!command.args.empty()) {
        std::wstring name = CommandParser::toLower(command.args[0]);
        if (name == L"cd" || name == L"chdir") {
            std::wcout << L"cd|chdir [path]\nChange or show the current directory.\n";
        } else if (name == L"dir") {
            std::wcout << L"dir [path|wildcard]\nList files, directories, and disk space.\n";
        } else if (name == L"history") {
            std::wcout << L"history [n|clear]\nShow all history, the last n commands, or clear history.\n";
        } else if (name == L"tasklist") {
            std::wcout << L"tasklist [keyword]\nShow running processes, optionally filtered by image name.\n";
        } else if (name == L"taskkill") {
            std::wcout << L"taskkill <pid>\ntaskkill /PID <pid> [/F]\ntaskkill /IM <image-name> [/F]\n";
        } else if (name == L"set") {
            std::wcout << L"set\nset NAME\nset NAME=VALUE\nList, show, set, or delete environment variables.\n";
        } else {
            std::wcout << L"No detailed help for " << command.args[0] << L"\n";
        }
        lastExitCode_ = 0;
        return;
    }

    std::wcout
        << L"Windows command shell\n"
        << L"Built-in commands:\n"
        << L"  cd|chdir [path]           Change or show current directory\n"
        << L"  dir [path|wildcard]       List files, directories, and disk space\n"
        << L"  mkdir|md <dir>            Create a directory\n"
        << L"  rmdir|rd <dir>            Remove an empty directory\n"
        << L"  del|erase <file>          Delete file(s), wildcard supported\n"
        << L"  copy <src> <dst>          Copy a file\n"
        << L"  move|ren|rename <src> <dst> Move or rename a file or directory\n"
        << L"  type <file>               Display a text file\n"
        << L"  history [n|clear]         Show, limit, or clear command history\n"
        << L"  tasklist [keyword]        Show running processes\n"
        << L"  taskkill <pid>            Terminate a process by PID\n"
        << L"  taskkill /IM <name>       Terminate processes by image name\n"
        << L"  set [name[=value]]        Manage environment variables\n"
        << L"  echo [text]               Print text and expand %VAR%, %CD%, %ERRORLEVEL%\n"
        << L"  date | time | ver         Show date, time, or shell version\n"
        << L"  pwd                       Show current directory\n"
        << L"  cls | clear               Clear the screen\n"
        << L"  exit [code]               Exit this shell\n"
        << L"  help|? [command]          Show help\n";
    lastExitCode_ = 0;
}

// cd/chdir：显示或切换当前工作目录。
void Shell::cmdCd(const ParsedCommand& command) {
    if (command.args.empty()) {
        std::wcout << winutil::getCurrentDirectory() << L"\n";
        lastExitCode_ = 0;
        return;
    }

    if (command.args[0] == L"/?") {
        std::wcout << L"Usage: cd|chdir [path]\n";
        lastExitCode_ = 0;
        return;
    }

    std::wstring target;
    if (equalsIgnoreCase(command.args[0], L"/d")) {
        // 兼容 Windows cmd 的 cd /d 写法；本程序当前目录本来就可以跨盘切换。
        if (command.args.size() < 2) {
            std::wcerr << L"cd: missing path after /d\n";
            lastExitCode_ = 1;
            return;
        }
        target = command.args[1];
    } else {
        target = command.args[0];
    }

    // cd 必须作为内部命令执行，因为当前目录是 Shell 进程自己的状态。
    if (!SetCurrentDirectoryW(target.c_str())) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"cd: cannot change directory to \"" << target << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    lastExitCode_ = 0;
}

// dir：枚举目录内容并显示卷信息、文件统计和剩余空间。
void Shell::cmdDir(const ParsedCommand& command) {
    if (!command.args.empty() && command.args[0] == L"/?") {
        std::wcout << L"Usage: dir [path|wildcard]\n";
        lastExitCode_ = 0;
        return;
    }

    std::wstring target = command.args.empty() ? L"." : command.args[0];
    // 目录路径会转换成“目录\*”，通配符路径保持用户输入原样。
    // 搜索模式
    std::wstring pattern = winutil::makeDirSearchPattern(target);
    // dir 输出中显示的目录路径
    std::wstring listingDir = winutil::getListingDirectory(target);
    // 获取卷根目录
    std::wstring volumeRoot = winutil::getVolumeRoot(listingDir);

    // Windows
    wchar_t volumeName[MAX_PATH + 1] = {};
    // 文件系统名称
    wchar_t fileSystemName[MAX_PATH + 1] = {};
    DWORD serialNumber = 0;
    // GetVolumeInformationW 用于显示卷标和卷序列号，模拟 Windows dir 的头部信息。
    if (!volumeRoot.empty() &&
        GetVolumeInformationW(volumeRoot.c_str(), volumeName, MAX_PATH, &serialNumber,
                              nullptr, nullptr, fileSystemName, MAX_PATH)) {
        std::wcout << L"Volume in drive " << volumeRoot.substr(0, 2);
        if (volumeName[0] != L'\0') {
            std::wcout << L" is " << volumeName;
        }
        std::wcout << L"\nVolume Serial Number is "
                   << winutil::formatVolumeSerial(serialNumber) << L"\n\n";
    }

    std::wcout << L"Directory of " << listingDir << L"\n\n";

    WIN32_FIND_DATAW data = {};
    // FindFirstFileW 接收“目录\*”或“*.cpp”这类模式，并返回第一项文件信息。
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"dir: cannot open \"" << target << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    std::uint64_t fileCount = 0;
    std::uint64_t dirCount = 0;
    std::uint64_t totalBytes = 0;

    do {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            // 跳过当前目录和父目录两个特殊项，统计结果更接近常用 dir 输出。
            continue;
        }

        // 目录项和普通文件使用不同格式：目录显示 <DIR>，文件统计字节数。
        bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wcout << winutil::formatFileTime(data.ftLastWriteTime) << L"  ";
        if (isDir) {
            ++dirCount;
            std::wcout << std::setw(14) << std::left << L"<DIR>" << std::right << L" ";
        } else {
            std::uint64_t size = fileSizeOf(data);
            ++fileCount;
            totalBytes += size;
            std::wcout << std::setw(14) << winutil::formatFileSize(size) << L" ";
        }
        std::wcout << name << L"\n";
    } while (FindNextFileW(find, &data));
    std::wcout.flush();

    DWORD lastFindError = GetLastError();
    FindClose(find);
    // FindNextFileW 正常结束也会设置 ERROR_NO_MORE_FILES，其它错误才算遍历失败。
    if (lastFindError != ERROR_NO_MORE_FILES) {
        std::wcerr << L"dir: enumeration stopped: "
                   << winutil::getLastErrorMessage(lastFindError) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    std::wcout << L"\n"
               << fileCount << L" File(s) "
               << winutil::formatUnsigned(totalBytes) << L" bytes\n"
               << dirCount << L" Dir(s) ";

    ULARGE_INTEGER freeBytesAvailable = {};
    ULARGE_INTEGER totalBytesOnDisk = {};
    ULARGE_INTEGER totalFreeBytes = {};
    // freeBytesAvailable：当前用户可用空间。
    // totalBytesOnDisk：磁盘总空间。
    // totalFreeBytes：磁盘总剩余空间。
    // GetDiskFreeSpaceExW 查询磁盘剩余空间，用 totalFreeBytes 显示整个卷的剩余字节数。
    if (!volumeRoot.empty() &&
        GetDiskFreeSpaceExW(volumeRoot.c_str(), &freeBytesAvailable,
                            &totalBytesOnDisk, &totalFreeBytes)) {
        std::wcout << winutil::formatUnsigned(totalFreeBytes.QuadPart)
                   << L" bytes free";
    } else {
        std::wcout << L"free space unavailable";
    }
    std::wcout << L"\n";
    std::wcout.flush();
    lastExitCode_ = 0;
}

// history：显示历史、最近 n 条历史，或清空历史。
void Shell::cmdHistory(const ParsedCommand& command) {
    if (!command.args.empty() && equalsIgnoreCase(command.args[0], L"clear")) {
        history_.clear();
        std::wcout << L"history cleared\n";
        lastExitCode_ = 0;
        return;
    }

    std::size_t begin = 0;
    if (!command.args.empty()) {
        std::size_t count = 0;
        if (!parseUnsigned(command.args[0], count)) {
            std::wcerr << L"history: invalid count \"" << command.args[0] << L"\"\n";
            lastExitCode_ = 1;
            return;
        }
        if (count < history_.size()) {
            // history n 只显示最近 n 条，因此从 size - n 开始遍历。
            begin = history_.size() - count;
        }
    }

    for (std::size_t i = begin; i < history_.size(); ++i) {
        std::wcout << std::setw(4) << (i + 1) << L"  " << history_[i] << L"\n";
    }
    lastExitCode_ = 0;
}

// tasklist：使用进程快照显示进程，可按进程名关键字过滤。
void Shell::cmdTasklist(const ParsedCommand& command) {
    std::wstring keyword;
    if (!command.args.empty()) {
        if (command.args[0] == L"/?") {
            std::wcout << L"Usage: tasklist [keyword]\n";
            lastExitCode_ = 0;
            return;
        }
        keyword = command.args[0];
    }

    // 创建进程快照，Tool Help 快照会复制当前进程列表，后续通过 Process32First/Next 遍历。
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"tasklist: cannot create process snapshot: "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    // PROCESSENTRY32W 是进程信息结构体，包含 PID、父进程 PID、线程数和可执行文件名等。
    PROCESSENTRY32W entry = {};
    // Windows 要求调用 Process32FirstW 前先设置 dwSize，否则 API 可能失败。
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"tasklist: cannot read process snapshot: "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        CloseHandle(snapshot);
        lastExitCode_ = 1;
        return;
    }

    std::wcout << std::left
               << std::setw(36) << L"Image Name"
               << std::right << std::setw(10) << L"PID"
               << std::setw(10) << L"Threads"
               << std::setw(12) << L"Parent PID" << L"\n";
    std::wcout << std::wstring(68, L'-') << L"\n";

    std::size_t shown = 0;
    do {
        std::wstring imageName = entry.szExeFile;
        if (!keyword.empty() && !containsIgnoreCase(imageName, keyword)) {
            // tasklist keyword 是包含匹配，不要求进程名完全相等。
            continue;
        }
        ++shown;
        std::wcout << std::left << std::setw(36) << imageName
                   << std::right << std::setw(10) << entry.th32ProcessID
                   << std::setw(10) << entry.cntThreads
                   << std::setw(12) << entry.th32ParentProcessID << L"\n";
    } while (Process32NextW(snapshot, &entry));

    DWORD lastSnapshotError = GetLastError();
    CloseHandle(snapshot);
    // Process32NextW 正常遍历结束时也会留下 ERROR_NO_MORE_FILES。
    if (lastSnapshotError != ERROR_NO_MORE_FILES) {
        std::wcerr << L"tasklist: enumeration stopped: "
                   << winutil::getLastErrorMessage(lastSnapshotError) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    if (!keyword.empty()) {
        std::wcout << shown << L" matching process(es)\n";
    }
    lastExitCode_ = 0;
}

// taskkill：按 PID 或进程名打开并终止目标进程。
void Shell::cmdTaskkill(const ParsedCommand& command) {
    if (command.args.empty() || command.args[0] == L"/?") {
        std::wcout << L"Usage: taskkill <pid>\n"
                   << L"       taskkill /PID <pid> [/F]\n"
                   << L"       taskkill /IM <image-name> [/F]\n";
        lastExitCode_ = command.args.empty() ? 1 : 0;
        return;
    }

    DWORD pid = 0;
    std::wstring imageName;
    // 支持三种形式：taskkill 123、taskkill /PID 123、taskkill /IM name.exe。
    for (std::size_t i = 0; i < command.args.size(); ++i) {
        if (equalsIgnoreCase(command.args[i], L"/f")) {
            // /F 在真实 taskkill 中表示强制终止；本实现本来就使用 TerminateProcess。
            continue;
        }
        // taskkill /PID 123 这种形式，/PID 后面必须跟数字参数。
        if (equalsIgnoreCase(command.args[i], L"/pid")) {
            if (i + 1 >= command.args.size()) {
                std::wcerr << L"taskkill: missing PID after /PID\n";
                lastExitCode_ = 1;
                return;
            }
            // ++i 先跳到 /PID 后面的数字参数，再解析成 DWORD。
            if (!winutil::tryParsePid(command.args[++i], pid) || pid == 0) {
                std::wcerr << L"taskkill: invalid PID \"" << command.args[i] << L"\"\n";
                lastExitCode_ = 1;
                return;
            }
            continue;
        }
        // taskkill /IM name.exe 这种形式，/IM 后面必须跟进程名参数。
        if (equalsIgnoreCase(command.args[i], L"/im")) {
            if (i + 1 >= command.args.size()) {
                std::wcerr << L"taskkill: missing image name after /IM\n";
                lastExitCode_ = 1;
                return;
            }
            imageName = command.args[++i];
            continue;
        }
        // taskkill 123 这种形式，直接把第一个参数当作 PID。
        if (!winutil::tryParsePid(command.args[i], pid) || pid == 0) {
            std::wcerr << L"taskkill: unknown argument or invalid PID \"" << command.args[i] << L"\"\n";
            lastExitCode_ = 1;
            return;
        }
    }

    // lambda 把“按 PID 终止进程”的公共逻辑封装起来，/PID 和 /IM 都能复用。
    auto terminatePid = [&](DWORD targetPid) -> bool {
        // taskkill 不允许终止当前 Shell 进程，否则会导致 Shell 自己退出。
        if (targetPid == GetCurrentProcessId()) {
            std::wcerr << L"taskkill: refusing to terminate this shell process\n";
            return false;
        }

        // 只申请终止和等待所需权限，权限不足时 OpenProcess 会失败并返回明确错误。
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, targetPid);
        if (process == nullptr) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"taskkill: cannot open process " << targetPid << L": "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            return false;
        }

        // TerminateProcess 是强制结束进程；第二个参数是目标进程的退出码。
        if (!TerminateProcess(process, 1)) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"taskkill: cannot terminate process " << targetPid << L": "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            CloseHandle(process);
            return false;
        }

        // 等待最多 2 秒，让终止动作有机会完成，再释放进程句柄。
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
        std::wcout << L"SUCCESS: terminated process with PID " << targetPid << L"\n";
        return true;
    };

    if (!imageName.empty()) {
        // /IM 按进程名结束时，先扫描快照收集所有同名进程 PID。
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"taskkill: cannot create process snapshot: "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            lastExitCode_ = 1;
            return;
        }

        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot, &entry)) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"taskkill: cannot read process snapshot: "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            CloseHandle(snapshot);
            lastExitCode_ = 1;
            return;
        }

        std::vector<DWORD> pids;
        do {
            if (equalsIgnoreCase(entry.szExeFile, imageName)) {
                // 先收集 PID，关闭快照后再逐个终止，避免边遍历边改变进程列表。
                pids.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));

        DWORD lastSnapshotError = GetLastError();
        CloseHandle(snapshot);
        if (lastSnapshotError != ERROR_NO_MORE_FILES) {
            std::wcerr << L"taskkill: enumeration stopped: "
                       << winutil::getLastErrorMessage(lastSnapshotError) << L"\n";
            lastExitCode_ = 1;
            return;
        }

        if (pids.empty()) {
            std::wcerr << L"taskkill: no process found with image name \"" << imageName << L"\"\n";
            lastExitCode_ = 1;
            return;
        }

        bool allOk = true;
        for (DWORD targetPid : pids) {
            // 保留所有终止结果：任意一个失败，最终 ERRORLEVEL 设为 1。
            allOk = terminatePid(targetPid) && allOk;
        }
        lastExitCode_ = allOk ? 0 : 1;
        return;
    }

    if (pid == 0) {
        std::wcerr << L"taskkill: missing PID or /IM image name\n";
        lastExitCode_ = 1;
        return;
    }

    lastExitCode_ = terminatePid(pid) ? 0 : 1;
}

// echo：输出文本，并展开常见环境变量引用。
void Shell::cmdEcho(const ParsedCommand& command) {
    if (command.args.empty()) {
        std::wcout << L"\n";
        lastExitCode_ = 0;
        return;
    }

    int previousExitCode = lastExitCode_;
    std::wstring text = CommandParser::join(command.args);
    // 先保存上一条退出码，避免 echo 自己把 lastExitCode_ 清零后影响 %ERRORLEVEL%。
    std::wcout << expandEnvironmentReferences(text, previousExitCode) << L"\n";
    lastExitCode_ = 0;
}

// pwd：显示当前工作目录。
void Shell::cmdPwd() {
    std::wcout << winutil::getCurrentDirectory() << L"\n";
    lastExitCode_ = 0;
}

// mkdir/md：创建目录。
void Shell::cmdMkdir(const ParsedCommand& command) {
    if (command.args.empty() || command.args[0] == L"/?") {
        std::wcout << L"Usage: mkdir <directory>\n";
        lastExitCode_ = command.args.empty() ? 1 : 0;
        return;
    }

    if (!CreateDirectoryW(command.args[0].c_str(), nullptr)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"mkdir: cannot create \"" << command.args[0] << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }
    lastExitCode_ = 0;
}

// rmdir/rd：删除空目录。
void Shell::cmdRmdir(const ParsedCommand& command) {
    if (command.args.empty() || command.args[0] == L"/?") {
        std::wcout << L"Usage: rmdir <directory>\n";
        lastExitCode_ = command.args.empty() ? 1 : 0;
        return;
    }

    if (!RemoveDirectoryW(command.args[0].c_str())) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"rmdir: cannot remove \"" << command.args[0] << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }
    lastExitCode_ = 0;
}

// del/erase：删除文件，支持通配符删除普通文件。
void Shell::cmdDel(const ParsedCommand& command) {
    if (command.args.empty() || command.args[0] == L"/?") {
        std::wcout << L"Usage: del <file|wildcard>\n";
        lastExitCode_ = command.args.empty() ? 1 : 0;
        return;
    }

    std::wstring target = command.args[0];
    if (!winutil::hasWildcard(target)) {
        // 没有通配符时直接删除单个文件；DeleteFileW 不能删除目录。
        if (!DeleteFileW(target.c_str())) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"del: cannot delete \"" << target << L"\": "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            lastExitCode_ = 1;
            return;
        }
        lastExitCode_ = 0;
        return;
    }

    WIN32_FIND_DATAW data = {};
    // 有通配符时先枚举匹配项，再逐个删除普通文件。
    HANDLE find = FindFirstFileW(target.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"del: cannot find \"" << target << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    std::wstring dir = winutil::directoryPart(target);
    std::size_t deleted = 0;
    bool allOk = true;
    do {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L".." || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // del 这里只删除文件，跳过目录，避免误删目录树。
            continue;
        }
        std::wstring fullPath = winutil::ensureTrailingBackslash(dir) + name;
        if (!DeleteFileW(fullPath.c_str())) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"del: cannot delete \"" << fullPath << L"\": "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            allOk = false;
        } else {
            ++deleted;
        }
    } while (FindNextFileW(find, &data));

    DWORD lastFindError = GetLastError();
    FindClose(find);
    // 通配符枚举同样需要区分“正常结束”和“中途出错”。
    if (lastFindError != ERROR_NO_MORE_FILES) {
        std::wcerr << L"del: enumeration stopped: "
                   << winutil::getLastErrorMessage(lastFindError) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    std::wcout << deleted << L" file(s) deleted\n";
    lastExitCode_ = allOk ? 0 : 1;
}

// copy：复制一个文件到目标路径。
void Shell::cmdCopy(const ParsedCommand& command) {
    if (command.args.size() < 2 || command.args[0] == L"/?") {
        std::wcout << L"Usage: copy <source-file> <destination-file>\n";
        lastExitCode_ = command.args.size() < 2 ? 1 : 0;
        return;
    }

    // 第三个参数 FALSE 表示如果目标已存在就覆盖，行为接近常见 copy 命令。
    if (!CopyFileW(command.args[0].c_str(), command.args[1].c_str(), FALSE)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"copy: cannot copy \"" << command.args[0] << L"\" to \""
                   << command.args[1] << L"\": " << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }
    std::wcout << L"1 file(s) copied\n";
    lastExitCode_ = 0;
}

// move/ren/rename：移动或重命名文件、目录。
void Shell::cmdMove(const ParsedCommand& command) {
    if (command.args.size() < 2 || command.args[0] == L"/?") {
        std::wcout << L"Usage: move|ren|rename <source> <destination>\n";
        lastExitCode_ = command.args.size() < 2 ? 1 : 0;
        return;
    }

    // MOVEFILE_COPY_ALLOWED 允许跨卷移动时退化为复制后删除；REPLACE_EXISTING 允许覆盖目标。
    if (!MoveFileExW(command.args[0].c_str(), command.args[1].c_str(),
                     MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"move: cannot move \"" << command.args[0] << L"\" to \""
                   << command.args[1] << L"\": " << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }
    std::wcout << L"1 item moved\n";
    lastExitCode_ = 0;
}

// type：读取并输出文本文件内容。
void Shell::cmdType(const ParsedCommand& command) {
    if (command.args.empty() || command.args[0] == L"/?") {
        std::wcout << L"Usage: type <text-file>\n";
        lastExitCode_ = command.args.empty() ? 1 : 0;
        return;
    }

    std::vector<char> bytes;
    std::wstring error;
    // type 分两步：先按字节读文件，再按 UTF-8 转成宽字符输出。
    if (!readFileBytes(command.args[0], bytes, error)) {
        std::wcerr << L"type: cannot read \"" << command.args[0] << L"\": " << error << L"\n";
        lastExitCode_ = 1;
        return;
    }

    std::wcout << bytesToText(bytes);
    if (!bytes.empty() && bytes.back() != '\n') {
        // 文件末尾没有换行时补一个换行，避免下一个提示符接在文件内容后面。
        std::wcout << L"\n";
    }
    lastExitCode_ = 0;
}

// set：枚举、查询、设置或删除当前进程环境变量。
void Shell::cmdSet(const ParsedCommand& command) {
    if (command.args.empty()) {
        LPWCH block = GetEnvironmentStringsW();
        if (block == nullptr) {
            DWORD errorCode = GetLastError();
            std::wcerr << L"set: cannot read environment: "
                       << winutil::getLastErrorMessage(errorCode) << L"\n";
            lastExitCode_ = 1;
            return;
        }

        // 环境块是以两个 L'\0' 结尾的字符串序列，每个变量项自身以 L'\0' 结束。
        for (LPWCH current = block; *current != L'\0'; current += wcslen(current) + 1) {
            if (*current != L'=') {
                std::wcout << current << L"\n";
            }
        }
        FreeEnvironmentStringsW(block);
        lastExitCode_ = 0;
        return;
    }

    std::wstring assignment = CommandParser::join(command.args);
    std::size_t equals = assignment.find(L'=');
    if (equals == std::wstring::npos) {
        // 没有等号表示查询单个变量；有等号才表示设置或删除变量。
        bool found = false;
        std::wstring value = getEnvironmentVariableValue(assignment, found);
        if (!found) {
            std::wcerr << L"set: environment variable not found: " << assignment << L"\n";
            lastExitCode_ = 1;
            return;
        }
        std::wcout << assignment << L"=" << value << L"\n";
        lastExitCode_ = 0;
        return;
    }

    std::wstring name = assignment.substr(0, equals);
    std::wstring value = assignment.substr(equals + 1);
    if (name.empty()) {
        std::wcerr << L"set: variable name cannot be empty\n";
        lastExitCode_ = 1;
        return;
    }

    LPCWSTR newValue = value.empty() ? nullptr : value.c_str();
    // SetEnvironmentVariableW 的值参数为 nullptr 时表示删除该环境变量。
    if (!SetEnvironmentVariableW(name.c_str(), newValue)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"set: cannot set \"" << name << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }
    lastExitCode_ = 0;
}

// date：显示当前本地日期。
void Shell::cmdDate() {
    SYSTEMTIME now = {};
    // GetLocalTime 读取本地时区时间，SYSTEMTIME 字段可直接格式化输出。
    GetLocalTime(&now);
    std::wcout << std::setfill(L'0')
               << std::setw(4) << now.wYear << L'/'
               << std::setw(2) << now.wMonth << L'/'
               << std::setw(2) << now.wDay << std::setfill(L' ') << L"\n";
    lastExitCode_ = 0;
}

// time：显示当前本地时间。
void Shell::cmdTime() {
    SYSTEMTIME now = {};
    // 这里只显示到秒，不处理毫秒字段。
    GetLocalTime(&now);
    std::wcout << std::setfill(L'0')
               << std::setw(2) << now.wHour << L':'
               << std::setw(2) << now.wMinute << L':'
               << std::setw(2) << now.wSecond << std::setfill(L' ') << L"\n";
    lastExitCode_ = 0;
}

// ver：显示解释器版本以及当前用户名、计算机名。
void Shell::cmdVer() {
    std::wcout << L"WinCommandShell version 2.0\n"
               << L"User: " << winutil::getUserNameSafe()
               << L"  Computer: " << winutil::getComputerNameSafe() << L"\n";
    lastExitCode_ = 0;
}

// cls/clear：使用控制台缓冲区 API 清屏并把光标移回左上角。
void Shell::cmdClear() {
    // 获取标准输出句柄，后续清屏操作都作用在这个控制台缓冲区上。
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE || output == nullptr) {
        std::wcerr << L"cls: cannot get console output handle\n";
        lastExitCode_ = 1;
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO info = {};
    // 读取控制台宽高和当前属性，清屏后仍保持原来的文字属性。
    if (!GetConsoleScreenBufferInfo(output, &info)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"cls: cannot read console buffer: "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    COORD origin = {0, 0};
    // 控制台缓冲区总格子数 = 列数 * 行数。
    DWORD cellCount = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
    DWORD written = 0;
    // 清屏分三步：填空格、恢复属性、把光标移动回左上角。
    if (!FillConsoleOutputCharacterW(output, L' ', cellCount, origin, &written) ||
        !FillConsoleOutputAttribute(output, info.wAttributes, cellCount, origin, &written) ||
        !SetConsoleCursorPosition(output, origin)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"cls: cannot clear console: "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    lastExitCode_ = 0;
}

// exit：退出 Shell，可指定进程退出码。
void Shell::cmdExit(const ParsedCommand& command) {
    if (!command.args.empty()) {
        DWORD code = 0;
        // 复用 PID 的数字解析函数，确保退出码只接受无符号十进制数字。
        if (!winutil::tryParsePid(command.args[0], code)) {
            std::wcerr << L"exit: invalid exit code \"" << command.args[0] << L"\"\n";
            lastExitCode_ = 1;
            return;
        }
        lastExitCode_ = static_cast<int>(code);
    }
    // 修改 running_ 后，Shell::run 的 while 循环会在本轮命令结束后退出。
    running_ = false;
}

// 执行外部命令：先直接 CreateProcessW，失败后用 cmd.exe /C 兼容执行。
void Shell::executeExternal(const ParsedCommand& command) {
    // CreateProcessW 要求命令行缓冲区可写，因此不能直接传 command.original.c_str()。
    std::vector<wchar_t> commandLine(command.original.begin(), command.original.end());
    commandLine.push_back(L'\0');

    STARTUPINFOW startupInfo = {};
    // cb 字段必须设置为结构体大小，这是 Win32 API 识别结构体版本的常见要求。
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

    // lpApplicationName 传 nullptr 时，系统会从命令行第一个 token 推断可执行文件。
    BOOL created = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created) {
        DWORD firstError = GetLastError();
        wchar_t comspecBuffer[MAX_PATH + 1] = {};
        // ComSpec 通常指向系统 cmd.exe；取不到时使用默认路径兜底。
        DWORD comspecLength = GetEnvironmentVariableW(L"ComSpec", comspecBuffer, MAX_PATH);
        std::wstring comspec = (comspecLength > 0 && comspecLength <= MAX_PATH)
                                   ? std::wstring(comspecBuffer, comspecLength)
                                   : L"C:\\Windows\\System32\\cmd.exe";
        // 直接 CreateProcessW 不能执行部分 shell 内建/批处理语法，所以失败后交给 cmd.exe /C。
        std::wstring fallback = L"\"" + comspec + L"\" /C " + command.original;
        std::vector<wchar_t> fallbackLine(fallback.begin(), fallback.end());
        fallbackLine.push_back(L'\0');

        created = CreateProcessW(
            nullptr,
            fallbackLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo);

        if (!created) {
            std::wcerr << command.name << L": command execution failed: "
                       << winutil::getLastErrorMessage(firstError) << L"\n";
            lastExitCode_ = 1;
            return;
        }
    }

    // Shell 采用同步执行：等待外部命令结束后再显示下一次提示符。
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    // 把外部进程退出码保存到 lastExitCode_，供 echo %ERRORLEVEL% 使用。
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        lastExitCode_ = static_cast<int>(exitCode);
    } else {
        lastExitCode_ = 1;
    }

    // CreateProcessW 成功后会返回进程句柄和主线程句柄，两者都需要关闭。
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
}
