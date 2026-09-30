#include "../console_output.h"
#include <fstream>
#include <filesystem>
#include <string>

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) return 20;
    // Launched with a new hidden console; give it a deterministic CP932 start.
    if (!SetConsoleOutputCP(932)) return 21;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO before{};
    if (!GetConsoleScreenBufferInfo(output, &before)) return 22;
    bool ok = true;
    std::string details;
    {
        ConsoleOutputEncoding encoding;
        ok = GetConsoleOutputCP() == CP_UTF8;
        std::cout << "日本語の表示確認" << std::flush;
        wchar_t text[32]{};
        DWORD count = 0;
        const bool read = ReadConsoleOutputCharacterW(output, text, 16, before.dwCursorPosition, &count);
        details = " cp=" + std::to_string(GetConsoleOutputCP()) + " read=" + std::to_string(read) + " count=" + std::to_string(count);
        for (DWORD i = 0; i < count; ++i) details += " " + std::to_string(unsigned(text[i]));
        ok = ok && read && count >= 8 && std::wstring(text, 8) == L"日本語の表示確認";
        std::cout << '\n' << std::flush;
        CONSOLE_SCREEN_BUFFER_INFO start{}, end{};
        GetConsoleScreenBufferInfo(output, &start);
        writeProgressLine(std::string(500, 'x') + "日本語");
        GetConsoleScreenBufferInfo(output, &end);
        ok = ok && start.dwCursorPosition.Y == end.dwCursorPosition.Y;
        writeProgressLine("短い表示");
    }
    ok = ok && GetConsoleOutputCP() == 932;
    if (argc >= 4) {
        // Run the shipped executable or launcher in this CP932 console.
        wchar_t comspec[32768]{};
        GetEnvironmentVariableW(L"COMSPEC", comspec, 32768);
        const bool launcher = std::wstring(argv[3]) == L"cmd";
        std::wstring command = launcher ? std::wstring(L"\"") + comspec + L"\" /d /c \"call run.cmd < nul\"" :
            std::wstring(L"\"") + argv[2] + L"\" config.ini";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &startup, &child)) return 24;
        const DWORD waited = WaitForSingleObject(child.hProcess, 15000);
        DWORD code = 99;
        GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        CONSOLE_SCREEN_BUFFER_INFO end{};
        GetConsoleScreenBufferInfo(output, &end);
        std::wstring rendered(size_t(end.dwSize.X) * (end.dwCursorPosition.Y + 1), L'\0');
        DWORD count = 0;
        ok = ok && waited == WAIT_OBJECT_0 && code == 0 &&
            ReadConsoleOutputCharacterW(output, rendered.data(), DWORD(rendered.size()), {0, 0}, &count);
        rendered.resize(count);
        ok = ok && rendered.find(L"目標数 1 件のユニークな1連鎖盤面を生成しました。") != std::wstring::npos;
        ok = ok && GetConsoleOutputCP() == 932;
        details += launcher ? " run.cmd rendered Japanese" : " exe rendered Japanese";
    }
    std::ofstream report{std::filesystem::path(argv[1])};
    report << (ok ? "console UTF-8 rendering, no-wrap progress, and CP restore passed" : "failed") << details;
    return ok ? 0 : 23;
}
