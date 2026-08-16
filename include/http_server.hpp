#pragma once

#include "http.hpp"
#include "thread_pool.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>

class HttpServer {
public:
    HttpServer(std::uint16_t port, std::filesystem::path document_root, std::size_t workers);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void run();
    void stop();
    std::uint16_t port() const noexcept;
    static void request_stop_from_signal() noexcept;

private:
    void handle_client(int client_fd);
    bool send_response(int client_fd, const http::HttpResponse& response);
    std::string stats_body() const;

    std::uint16_t port_;
    std::filesystem::path document_root_;
    ThreadPool pool_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint16_t> bound_port_{0};
    int listen_fd_ = -1;
    mutable std::mutex clients_mutex_;
    std::set<int> client_fds_;
    std::atomic<std::uint64_t> request_count_{0};
    std::atomic<std::uint64_t> error_count_{0};
    static volatile std::sig_atomic_t signal_stop_requested_;
};
