#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Shell.h"

#include <windows.h>

#include <clocale>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <locale>
#include <stdexcept>

int main() {
    // Windows 控制台默认窄字符模式容易导致中文输入输出乱码，这里切到 UTF-16 宽字符模式。
    _setmode(_fileno(stdin), _O_U16TEXT);
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    // 使用系统本地 locale，让 C++ 宽字符流尽量跟控制台环境保持一致。
    std::setlocale(LC_ALL, "");
    try {
        std::locale::global(std::locale(""));
        std::wcin.imbue(std::locale());
        std::wcout.imbue(std::locale());
        std::wcerr.imbue(std::locale());
    } catch (const std::runtime_error&) {
        // 如果系统没有可用的本地 locale，就保留默认 locale。
    }

    Shell shell;
    return shell.run();
}
