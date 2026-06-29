#include "CommandParser.h"

#include <cwctype>
#include <locale>
#include <sstream>

// 去除用户输入首尾的空白字符。
std::wstring CommandParser::trim(const std::wstring& value) {
    std::size_t first = 0;
    while (first < value.size() && std::iswspace(value[first])) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) {
        --last;
    }

    return value.substr(first, last - first);
}

// 统一命令名大小写，便于内部命令忽略大小写匹配。
std::wstring CommandParser::toLower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

// 将多个参数重新拼成文本，供 echo/set 使用。
std::wstring CommandParser::join(const std::vector<std::wstring>& values, std::size_t begin) {
    std::wostringstream oss;
    for (std::size_t i = begin; i < values.size(); ++i) {
        if (i != begin) {
            oss << L' ';
        }
        oss << values[i];
    }
    return oss.str();
}

// 将原始输入行转换为命令名、参数列表和原始命令文本。
ParsedCommand CommandParser::parse(const std::wstring& line) {
    ParsedCommand result;
    result.original = trim(line);
    if (result.original.empty()) {
        return result;
    }

    std::vector<std::wstring> tokens = tokenize(result.original);
    if (tokens.empty()) {
        return result;
    }

    // 第一个 token 永远视为命令名，其余 token 作为参数交给具体命令处理。
    result.empty = false;
    result.name = toLower(tokens.front());
    result.args.assign(tokens.begin() + 1, tokens.end());
    return result;
}

// 按空白切分命令，同时保留 "C:\Program Files" 这类引号路径。
std::vector<std::wstring> CommandParser::tokenize(const std::wstring& line) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        wchar_t ch = line[i];

        if (ch == L'"') {
            // 在引号内部遇到两个连续双引号时，解释为参数中的一个真实双引号。
            if (inQuotes && i + 1 < line.size() && line[i + 1] == L'"') {
                current.push_back(L'"');
                ++i;
            } else {
                // 普通双引号只用于切换“是否处于引号内”的状态，不写入参数。
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (!inQuotes && std::iswspace(ch)) {
            // 引号外的空白是参数分隔符；连续空白不会产生空参数。
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}
