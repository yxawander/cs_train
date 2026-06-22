#pragma once

#include <string>
#include <vector>

struct ParsedCommand {
    std::wstring original;
    std::wstring name;
    std::vector<std::wstring> args;
    bool empty = true;
};

class CommandParser {
public:
    static ParsedCommand parse(const std::wstring& line);
    static std::wstring trim(const std::wstring& value);
    static std::wstring toLower(std::wstring value);
    static std::wstring join(const std::vector<std::wstring>& values, std::size_t begin = 0);

private:
    static std::vector<std::wstring> tokenize(const std::wstring& line);
};

