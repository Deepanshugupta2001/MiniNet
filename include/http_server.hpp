#pragma once

#include "thread_pool.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>

class HttpServer {
public:
    HttpServer(std::uint16_t port, std::filesystem::path document_root, std::size_t workers);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void run();
    void stop();

private:
    void handle_client(int client_fd);

    std::uint16_t port_;
    std::filesystem::path document_root_;
    ThreadPool pool_;
    std::atomic<bool> running_{false};
    int listen_fd_ = -1;
};
