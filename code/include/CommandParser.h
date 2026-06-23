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
    // 将一整行输入解析为命令名和参数列表。
    static ParsedCommand parse(const std::wstring& line);
    // 去除字符串首尾空白字符。
    static std::wstring trim(const std::wstring& value);
    // 转成小写，便于命令和选项做忽略大小写匹配。
    static std::wstring toLower(std::wstring value);
    // 将多个参数重新拼成一段文本，主要用于 echo 和 set。
    static std::wstring join(const std::vector<std::wstring>& values, std::size_t begin = 0);

private:
    // 按空白切分命令行，同时保留双引号中的空格。
    static std::vector<std::wstring> tokenize(const std::wstring& line);
};
