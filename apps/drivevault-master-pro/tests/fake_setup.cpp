// Test double for a product's setup / uninstall program: writes its command line into the file named by DM_FAKE_MARKER and exits with DM_FAKE_EXIT.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <windows.h>

int main()
{
    char marker[MAX_PATH] = {};
    char code[16] = {};
    GetEnvironmentVariableA("DM_FAKE_MARKER", marker, sizeof(marker));
    GetEnvironmentVariableA("DM_FAKE_EXIT", code, sizeof(code));
    if (marker[0]) {
        if (FILE* f = std::fopen(marker, "ab")) {
            const std::string line = std::string(GetCommandLineA()) + "\n";
            std::fwrite(line.data(), 1, line.size(), f);
            std::fclose(f);
        }
    }
    return code[0] ? std::atoi(code) : 0;
}
