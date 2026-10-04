#pragma once
#include <iostream>
#include <string>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Source strings and redirected logs use UTF-8. A Japanese Windows console
// normally starts in CP932, so configure attached consoles for UTF-8 as well.
class ConsoleOutputEncoding {
#ifdef _WIN32
    UINT previousCodePage = 0;
    bool changed = false;
#endif
public:
    ConsoleOutputEncoding() {
#ifdef _WIN32
        DWORD mode = 0;
        if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) ||
            GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode)) {
            previousCodePage = GetConsoleOutputCP();
            changed = previousCodePage && previousCodePage != CP_UTF8 && SetConsoleOutputCP(CP_UTF8);
        }
#endif
    }
    ~ConsoleOutputEncoding() {
        std::cout.flush();
        std::cerr.flush();
#ifdef _WIN32
        if (changed) SetConsoleOutputCP(previousCodePage);
#endif
    }
    ConsoleOutputEncoding(const ConsoleOutputEncoding&) = delete;
    ConsoleOutputEncoding& operator=(const ConsoleOutputEncoding&) = delete;
};

inline void writeProgressLine(const std::string& text) {
    size_t maxColumns = 1000000;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        // Leave the final column empty so CR updates never wrap to a new row.
        maxColumns = size_t(std::max(0, std::min(int(info.dwSize.X),
            int(info.srWindow.Right - info.srWindow.Left + 1)) - 1));
    }
#endif
    size_t bytes = 0, columns = 0;
    while (bytes < text.size()) {
        const auto c = static_cast<unsigned char>(text[bytes]);
        const size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        // Progress text contains ASCII and Japanese full-width characters.
        const size_t width = c < 0x80 ? 1 : 2;
        if (columns + width > maxColumns) break;
        bytes += length;
        columns += width;
    }
    static size_t previousColumns = 0;
    const size_t padding = std::min(maxColumns, previousColumns) > columns ?
        std::min(maxColumns, previousColumns) - columns : 0;
    std::cout << '\r' << text.substr(0, bytes) << std::string(padding, ' ') << std::flush;
    previousColumns = columns;
}
