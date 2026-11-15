// A stand-in for `claude` / `codex`: reads the prompt from standard input and prints the JSON event lines the real programs print.
// DM_FAKE_MODE: claude-ok | claude-error | codex-ok | echo | hang ; DM_FAKE_ARGS = file that receives the command line.
#define _CRT_SECURE_NO_WARNINGS
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    if (const char* argsFile = std::getenv("DM_FAKE_ARGS")) {
        std::ofstream out(argsFile, std::ios::app);
        for (int i = 1; i < argc; ++i)
            out << argv[i] << '\n';
        out << "--end--\n";
    }
    const std::string prompt((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    const char* modeEnv = std::getenv("DM_FAKE_MODE");
    const std::string mode = modeEnv ? modeEnv : "claude-ok";
    auto line = [](const std::string& s) { std::fputs((s + "\n").c_str(), stdout); std::fflush(stdout); };
    if (mode == "hang") {
        line(R"({"type":"system","subtype":"init","session_id":"11111111-2222-3333-4444-555555555555"})");
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 0;
    }
    if (mode == "echo") {
        std::string escaped;
        for (char c : prompt) {
            if (c == '"' || c == '\\')
                escaped += '\\';
            if (c == '\n')
                escaped += "\\n";
            else
                escaped += c;
        }
        line(R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":")" + escaped + R"("}}})");
        line(R"({"type":"result","subtype":"success","is_error":false,"session_id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"})");
        return 0;
    }
    if (mode == "claude-error") {
        line(R"({"type":"result","subtype":"error","is_error":true,"result":"Not logged in","session_id":"x"})");
        return 1;
    }
    if (mode == "codex-ok") {
        line(R"({"type":"thread.started","thread_id":"th-1"})");
        line(R"({"type":"item.started","item":{"id":"1","type":"command_execution","command":"dir"}})");
        line(R"({"type":"item.completed","item":{"id":"2","type":"agent_message","text":"Done."}})");
        line(R"({"type":"turn.completed","usage":{}})");
        return 0;
    }
    line(R"({"type":"system","subtype":"init","session_id":"11111111-2222-3333-4444-555555555555"})");
    line(R"({"type":"stream_event","event":{"type":"content_block_start","content_block":{"type":"text"}}})");
    line(R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Hel"}}})");
    line(R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"lo"}}})");
    line(R"({"type":"assistant","message":{"content":[{"type":"text","text":"Hello"},{"type":"tool_use","name":"Read","input":{"file_path":"notes.txt"}}]}})");
    line(R"({"type":"stream_event","event":{"type":"content_block_start","content_block":{"type":"text"}}})");
    line(R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"World"}}})");
    line(R"({"type":"assistant","message":{"content":[{"type":"text","text":"World"}]}})");
    line(R"({"type":"result","subtype":"success","is_error":false,"result":"HelloWorld","session_id":"11111111-2222-3333-4444-555555555555"})");
    return 0;
}
