#include "../generator/generator.h"

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    return puyo::runGenerator(argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path(L"config.ini"));
}
#else
int main(int argc, char* argv[]) {
    return puyo::runGenerator(argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path("config.ini"));
}
#endif
