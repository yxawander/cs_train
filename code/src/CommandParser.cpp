#include "CommandParser.h"

#include <cwctype>
#include <locale>
#include <sstream>

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

std::wstring CommandParser::toLower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

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

    result.empty = false;
    result.name = toLower(tokens.front());
    result.args.assign(tokens.begin() + 1, tokens.end());
    return result;
}

std::vector<std::wstring> CommandParser::tokenize(const std::wstring& line) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        wchar_t ch = line[i];

        if (ch == L'"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == L'"') {
                current.push_back(L'"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (!inQuotes && std::iswspace(ch)) {
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

