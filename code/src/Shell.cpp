#include "Shell.h"

#include "WinUtil.h"

#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

std::uint64_t fileSizeOf(const WIN32_FIND_DATAW& data) {
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
           static_cast<std::uint64_t>(data.nFileSizeLow);
}

bool equalsIgnoreCase(const std::wstring& left, const std::wstring& right) {
    return CommandParser::toLower(left) == CommandParser::toLower(right);
}

BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        std::wcout << L"\n";
        return TRUE;
    }
    return FALSE;
}

} // namespace

int Shell::run() {
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    while (running_) {
        printPrompt();

        std::wstring line;
        if (!std::getline(std::wcin, line)) {
            std::wcout << L"\n";
            break;
        }

        processLine(line);
    }

    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
    return lastExitCode_;
}

bool Shell::processLine(const std::wstring& line) {
    ParsedCommand command = CommandParser::parse(line);
    if (command.empty) {
        return running_;
    }

    history_.push_back(command.original);

    if (!dispatchBuiltin(command)) {
        executeExternal(command);
    }

    return running_;
}

void Shell::printPrompt() const {
    std::wcout << winutil::getCurrentDirectory() << L"> ";
    std::wcout.flush();
}

bool Shell::dispatchBuiltin(const ParsedCommand& command) {
    if (command.name == L"help" || command.name == L"?") {
        cmdHelp();
    } else if (command.name == L"cd" || command.name == L"chdir") {
        cmdCd(command);
    } else if (command.name == L"dir") {
        cmdDir(command);
    } else if (command.name == L"history") {
        cmdHistory(command);
    } else if (command.name == L"exit") {
        if (!command.args.empty()) {
            DWORD code = 0;
            if (winutil::tryParsePid(command.args[0], code)) {
                lastExitCode_ = static_cast<int>(code);
            }
        }
        running_ = false;
    } else if (command.name == L"tasklist") {
        cmdTasklist();
    } else if (command.name == L"taskkill") {
        cmdTaskkill(command);
    } else if (command.name == L"echo") {
        cmdEcho(command);
    } else if (command.name == L"pwd") {
        cmdPwd();
    } else if (command.name == L"cls") {
        system("cls");
    } else {
        return false;
    }

    return true;
}

void Shell::cmdHelp() const {
    std::wcout
        << L"Windows command shell\n"
        << L"Built-in commands:\n"
        << L"  cd [path]                 Change or show current directory\n"
        << L"  dir [path|wildcard]       List files, directories, and disk space\n"
        << L"  history [clear]           Show or clear command history\n"
        << L"  tasklist                  Show running processes\n"
        << L"  taskkill <pid>            Terminate a process by PID\n"
        << L"  taskkill /PID <pid>       Terminate a process by PID\n"
        << L"  echo [text]               Print text or %ERRORLEVEL%\n"
        << L"  pwd                       Show current directory\n"
        << L"  cls                       Clear the screen\n"
        << L"  exit [code]               Exit this shell\n";
}

void Shell::cmdCd(const ParsedCommand& command) {
    if (command.args.empty()) {
        std::wcout << winutil::getCurrentDirectory() << L"\n";
        lastExitCode_ = 0;
        return;
    }

    if (command.args[0] == L"/?") {
        std::wcout << L"Usage: cd [path]\n";
        lastExitCode_ = 0;
        return;
    }

    std::wstring target;
    if (equalsIgnoreCase(command.args[0], L"/d")) {
        if (command.args.size() < 2) {
            std::wcerr << L"cd: missing path after /d\n";
            lastExitCode_ = 1;
            return;
        }
        target = command.args[1];
    } else {
        target = command.args[0];
    }

    if (!SetCurrentDirectoryW(target.c_str())) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"cd: cannot change directory to \"" << target << L"\": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    lastExitCode_ = 0;
}

void Shell::cmdDir(const ParsedCommand& command) {
    if (!command.args.empty() && command.args[0] == L"/?") {
        std::wcout << L"Usage: dir [path|wildcard]\n";
        lastExitCode_ = 0;
        return;
    }

    std::wstring target = command.args.empty() ? L"." : command.args[0];
    std::wstring pattern = winutil::makeDirSearchPattern(target);
    std::wstring listingDir = winutil::getListingDirectory(target);
    std::wstring volumeRoot = winutil::getVolumeRoot(listingDir);

    wchar_t volumeName[MAX_PATH + 1] = {};
    wchar_t fileSystemName[MAX_PATH + 1] = {};
    DWORD serialNumber = 0;
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
            continue;
        }

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
    if (!volumeRoot.empty() &&
        GetDiskFreeSpaceExW(volumeRoot.c_str(), &freeBytesAvailable,
                            &totalBytesOnDisk, &totalFreeBytes)) {
        std::wcout << std::setw(16)
                   << winutil::formatUnsigned(totalFreeBytes.QuadPart)
                   << L" bytes free";
    } else {
        std::wcout << L"free space unavailable";
    }
    std::wcout << L"\n";
    std::wcout.flush();
    lastExitCode_ = 0;
}

void Shell::cmdHistory(const ParsedCommand& command) {
    if (!command.args.empty() && equalsIgnoreCase(command.args[0], L"clear")) {
        history_.clear();
        std::wcout << L"history cleared\n";
        lastExitCode_ = 0;
        return;
    }

    for (std::size_t i = 0; i < history_.size(); ++i) {
        std::wcout << std::setw(4) << (i + 1) << L"  " << history_[i] << L"\n";
    }
    lastExitCode_ = 0;
}

void Shell::cmdTasklist() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"tasklist: cannot create process snapshot: "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    PROCESSENTRY32W entry = {};
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

    do {
        std::wcout << std::left << std::setw(36) << entry.szExeFile
                   << std::right << std::setw(10) << entry.th32ProcessID
                   << std::setw(10) << entry.cntThreads
                   << std::setw(12) << entry.th32ParentProcessID << L"\n";
    } while (Process32NextW(snapshot, &entry));

    DWORD lastSnapshotError = GetLastError();
    CloseHandle(snapshot);
    if (lastSnapshotError != ERROR_NO_MORE_FILES) {
        std::wcerr << L"tasklist: enumeration stopped: "
                   << winutil::getLastErrorMessage(lastSnapshotError) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    lastExitCode_ = 0;
}

void Shell::cmdTaskkill(const ParsedCommand& command) {
    if (command.args.empty() || command.args[0] == L"/?") {
        std::wcout << L"Usage: taskkill <pid>\n"
                   << L"       taskkill /PID <pid>\n";
        lastExitCode_ = command.args.empty() ? 1 : 0;
        return;
    }

    std::wstring pidText;
    if (equalsIgnoreCase(command.args[0], L"/pid")) {
        if (command.args.size() < 2) {
            std::wcerr << L"taskkill: missing PID after /PID\n";
            lastExitCode_ = 1;
            return;
        }
        pidText = command.args[1];
    } else {
        pidText = command.args[0];
    }

    DWORD pid = 0;
    if (!winutil::tryParsePid(pidText, pid) || pid == 0) {
        std::wcerr << L"taskkill: invalid PID \"" << pidText << L"\"\n";
        lastExitCode_ = 1;
        return;
    }

    if (pid == GetCurrentProcessId()) {
        std::wcerr << L"taskkill: refusing to terminate this shell process\n";
        lastExitCode_ = 1;
        return;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"taskkill: cannot open process " << pid << L": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        lastExitCode_ = 1;
        return;
    }

    if (!TerminateProcess(process, 1)) {
        DWORD errorCode = GetLastError();
        std::wcerr << L"taskkill: cannot terminate process " << pid << L": "
                   << winutil::getLastErrorMessage(errorCode) << L"\n";
        CloseHandle(process);
        lastExitCode_ = 1;
        return;
    }

    WaitForSingleObject(process, 2000);
    CloseHandle(process);
    std::wcout << L"SUCCESS: terminated process with PID " << pid << L"\n";
    lastExitCode_ = 0;
}

void Shell::cmdEcho(const ParsedCommand& command) {
    if (command.args.empty()) {
        std::wcout << L"\n";
        lastExitCode_ = 0;
        return;
    }

    std::wstring text = CommandParser::join(command.args);
    if (equalsIgnoreCase(text, L"%errorlevel%")) {
        std::wcout << lastExitCode_ << L"\n";
    } else {
        std::wcout << text << L"\n";
    }
    lastExitCode_ = 0;
}

void Shell::cmdPwd() const {
    std::wcout << winutil::getCurrentDirectory() << L"\n";
}

void Shell::executeExternal(const ParsedCommand& command) {
    std::vector<wchar_t> commandLine(command.original.begin(), command.original.end());
    commandLine.push_back(L'\0');

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

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
        DWORD comspecLength = GetEnvironmentVariableW(L"ComSpec", comspecBuffer, MAX_PATH);
        std::wstring comspec = (comspecLength > 0 && comspecLength <= MAX_PATH)
                                   ? std::wstring(comspecBuffer, comspecLength)
                                   : L"C:\\Windows\\System32\\cmd.exe";
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

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        lastExitCode_ = static_cast<int>(exitCode);
    } else {
        lastExitCode_ = 1;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
}
