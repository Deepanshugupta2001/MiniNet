#include "http_server.hpp"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <thread>

namespace {
void handle_shutdown_signal(int) {
    HttpServer::request_stop_from_signal();
}
} // namespace

int main(int argc, char* argv[]) {
    const auto port = static_cast<std::uint16_t>(argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 8080);
    const auto root = argc > 2 ? argv[2] : "public";
    const auto workers = static_cast<std::size_t>(argc > 3 ? std::strtoul(argv[3], nullptr, 10) : std::thread::hardware_concurrency());
    std::signal(SIGPIPE, SIG_IGN);
    try {
        HttpServer server(port, root, workers == 0 ? 4 : workers);
        struct sigaction action {};
        action.sa_handler = handle_shutdown_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0; // Do not restart accept(); run() observes the requested shutdown.
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "MiniNet failed: " << error.what() << '\n';
        return 1;
    }
}
