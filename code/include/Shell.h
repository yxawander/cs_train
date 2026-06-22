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

    void cmdHelp() const;
    void cmdCd(const ParsedCommand& command);
    void cmdDir(const ParsedCommand& command);
    void cmdHistory(const ParsedCommand& command);
    void cmdTasklist();
    void cmdTaskkill(const ParsedCommand& command);
    void cmdEcho(const ParsedCommand& command);
    void cmdPwd() const;
    void executeExternal(const ParsedCommand& command);
};

