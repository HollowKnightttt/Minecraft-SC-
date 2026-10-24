#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "Log.h"
#include "Server.h"

#ifndef SERVER_VERSION
#define SERVER_VERSION "dev"
#endif

namespace {
bedrock::Server* g_server = nullptr;
void onSignal(int) { if (g_server) g_server->requestStop(); }   // hanya set atomic flag

void usage(const char* exe) {
    std::fprintf(stderr,
        "Pemakaian: %s [--port N] [--motd TEKS] [--level NAMA] [--max-players N]\n", exe);
}
} // namespace

int main(int argc, char** argv) {
    bedrock::Server::Config cfg;
    for (int i = 1; i < argc; ++i) {
        const bool hasValue = i + 1 < argc;
        if      (!std::strcmp(argv[i], "--port")        && hasValue) cfg.port = uint16_t(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--motd")        && hasValue) cfg.motd = argv[++i];
        else if (!std::strcmp(argv[i], "--level")       && hasValue) cfg.levelName = argv[++i];
        else if (!std::strcmp(argv[i], "--max-players") && hasValue) cfg.maxPlayers = std::atoi(argv[++i]);
        else { usage(argv[0]); return 1; }
    }

    LOG_INFO("Bedrock C++ Server v%s", SERVER_VERSION);

    bedrock::Server server(cfg);
    g_server = &server;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (!server.start()) return 2;
    server.run();
    return 0;
}
