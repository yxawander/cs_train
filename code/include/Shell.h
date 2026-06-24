#pragma once

#include "CommandParser.h"

#include <string>
#include <vector>

class Shell {
public:
    // 启动交互式命令循环。
    int run();
    // 处理一行用户输入的命令。
    bool processLine(const std::wstring& line);

private:
    bool running_ = true;
    int lastExitCode_ = 0;
    std::vector<std::wstring> history_;

    // 打印“当前目录>”格式的提示符。
    void printPrompt() const;
    // 将解析后的命令分发给对应的内部命令处理函数。
    bool dispatchBuiltin(const ParsedCommand& command);

    // 内部命令处理函数。
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
    void cmdClear();
    void cmdExit(const ParsedCommand& command);
    // 通过 CreateProcessW 执行外部命令，失败后尝试 cmd.exe /C 兼容执行。
    void executeExternal(const ParsedCommand& command);
};
