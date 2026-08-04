#include <iostream>
#include <string>

#include "common/ServerApp.h"
#include "core/Core.h"
#include "testclient/ClientApp.h"

using namespace nhn;
using namespace nhn::test;

int main(int argc, char** argv) {
    if (!ServerApp::Bootstrap(argc, argv, "TestClient")) {
        return 1;
    }

    Config& config = ServerApp::GetConfig();

    ClientApp::Settings settings;
    settings.matchHost = config.GetString("match-host", "127.0.0.1");
    settings.matchPort = config.GetPort("match-port", 7777);
    settings.nickname = config.GetString("nick", "player");
    settings.voiceFrameMs = static_cast<uint32>(config.GetInt("voice-frame-ms", 20));
    settings.voicePayloadBytes = static_cast<uint32>(config.GetInt("voice-payload", 60));

    ClientApp app(settings);
    GClientApp = &app;
    app.Start();

    Console::Print("NHN test client — 'help' for commands, 'quit' to exit");

    // --script runs a file and exits, which is what makes a four-player
    // scenario a matter of launching four processes from one batch file.
    const std::string script = config.GetString("script", "");
    if (!script.empty()) {
        app.RunScript(script);
        if (!config.GetBool("interactive", false)) {
            app.Stop();
            ServerApp::Shutdown();
            GClientApp = nullptr;
            return 0;
        }
    }

    if (!config.Has("nick")) {
        Console::Print("(nickname is '{}'; change it with 'nick <name>' before connecting)",
                       settings.nickname);
    }

    std::string line;
    while (!ServerApp::IsShuttingDown()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;  // stdin closed
        }
        if (!app.ExecuteCommand(line)) {
            break;
        }
    }

    app.Stop();
    ServerApp::Shutdown();
    GClientApp = nullptr;
    return 0;
}
