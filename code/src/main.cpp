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
    _setmode(_fileno(stdin), _O_U16TEXT);
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    std::setlocale(LC_ALL, "");
    try {
        std::locale::global(std::locale(""));
        std::wcin.imbue(std::locale());
        std::wcout.imbue(std::locale());
        std::wcerr.imbue(std::locale());
    } catch (const std::runtime_error&) {
        // Keep the classic locale if the host has no configured native locale.
    }

    Shell shell;
    return shell.run();
}
