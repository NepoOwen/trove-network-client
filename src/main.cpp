// File: main.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "app/session.hpp"
#include <cstdio>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace {

// Resolves to the executable's own folder, not the caller's cwd, so
// auth/<account_id>_raw.bin always lands next to the binary regardless of
// where the exe was launched from.
void chdir_to_exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return;
    std::filesystem::path exe_path(buf);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    std::filesystem::path exe_path(buf);
#endif
    std::error_code ec;
    std::filesystem::current_path(exe_path.parent_path(), ec);
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // flush every line immediately, not just on exit
    chdir_to_exe_dir();
    return app::run(argc, argv);
}
