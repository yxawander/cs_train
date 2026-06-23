#pragma once

#include "CommandParser.h"

#include <string>
#include <vector>

class Shell {
public:
    int run();
    bool processLine(const std::wstring& line);

private:
    bool running_ = true;
    int lastExitCode_ = 0;
    std::vector<std::wstring> history_;

    void printPrompt() const;
    bool dispatchBuiltin(const ParsedCommand& command);
    bool processLineInternal(const std::wstring& line, bool recordHistory);

    void cmdHelp(const ParsedCommand& command);
    void cmdCd(const ParsedCommand& command);
    void cmdDir(const ParsedCommand& command);
    void cmdHistory(const ParsedCommand& command);
    void cmdTasklist(const ParsedCommand& command);
    void cmdTaskkill(const ParsedCommand& command);
    void cmdEcho(const ParsedCommand& command);
    void cmdPwd();
    void cmdMkdir(const ParsedCommand& command);
    void cmdRmdir(const ParsedCommand& command);
    void cmdDel(const ParsedCommand& command);
    void cmdCopy(const ParsedCommand& command);
    void cmdMove(const ParsedCommand& command);
    void cmdType(const ParsedCommand& command);
    void cmdSet(const ParsedCommand& command);
    void cmdDate();
    void cmdTime();
    void cmdVer();
    void cmdExit(const ParsedCommand& command);
    void executeExternal(const ParsedCommand& command);
};
