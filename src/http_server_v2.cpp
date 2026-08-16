#include "http_server.hpp"

#include "http.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr std::size_t kMaxHeaderBytes = 16 * 1024;

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto result = send(fd, data.data() + sent, data.size() - sent, flags);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

http::Response error_response(int status, const std::string& reason, const std::string& body, bool include_body) {
    return {status, reason, body, "text/plain; charset=utf-8", include_body, {}};
}

bool is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *root_part != *candidate_part) return false;
    }
    return true;
}
} // namespace

volatile std::sig_atomic_t HttpServer::signal_stop_requested_ = 0;

HttpServer::HttpServer(std::uint16_t port, std::filesystem::path document_root, std::size_t workers)
    : port_(port), document_root_(std::filesystem::canonical(std::move(document_root))), pool_(workers) {}

HttpServer::~HttpServer() { stop(); }

std::uint16_t HttpServer::port() const noexcept { return bound_port_.load(); }

void HttpServer::request_stop_from_signal() noexcept { signal_stop_requested_ = 1; }

std::string HttpServer::stats_body() const {
    std::size_t active_connections = 0;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        active_connections = client_fds_.size();
    }
    return "{\"requests\":" + std::to_string(request_count_.load()) +
           ",\"errors\":" + std::to_string(error_count_.load()) +
           ",\"active_connections\":" + std::to_string(active_connections) + "}\n";
}

bool HttpServer::send_response(int client_fd, const http::HttpResponse& response) {
    if (response.status >= 400) error_count_.fetch_add(1);
    if (send_all(client_fd, http::serialize_response(response))) return true;
    const int error = errno;
    std::cerr << "send failed: " << std::strerror(error) << '\n';
    return false;
}

void HttpServer::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error(std::strerror(errno));
    int enabled = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(listen_fd_, SOMAXCONN) < 0) {
        const std::string error = std::strerror(errno);
        close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(error);
    }
    socklen_t address_size = sizeof(address);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_size) == 0) bound_port_ = ntohs(address.sin_port);
    running_ = true;
    std::cout << "MiniNet listening on http://0.0.0.0:" << bound_port_ << '\n';
    while (running_) {
        if (signal_stop_requested_ != 0) {
            stop();
            break;
        }
        const int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!running_) break;
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (!running_) { close(client_fd); break; }
            client_fds_.insert(client_fd);
        }
        if (!pool_.submit([this, client_fd] { handle_client(client_fd); })) {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.erase(client_fd);
            close(client_fd);
        }
    }
    stop();
}

void HttpServer::stop() {
    const bool was_running = running_.exchange(false);
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int client_fd : client_fds_) shutdown(client_fd, SHUT_RDWR);
    }
    pool_.shutdown(true);
    if (was_running) std::cout << "MiniNet stopped\n";
}

void HttpServer::handle_client(int client_fd) {
    const auto close_client = [this, client_fd] {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_fds_.erase(client_fd);
        close(client_fd);
    };
    if (!running_) { close_client(); return; }
    request_count_.fetch_add(1);
    std::string request;
    char buffer[4096];
    while (request.size() < kMaxHeaderBytes && request.find("\r\n\r\n") == std::string::npos) {
        const auto received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) { close_client(); return; }
        request.append(buffer, static_cast<std::size_t>(received));
    }
    if (request.find("\r\n\r\n") == std::string::npos) {
        send_response(client_fd, error_response(431, "Request Header Fields Too Large", "Headers too large\n", true));
        close_client();
        return;
    }
    const auto parsed = http::parse_request(request.substr(0, request.find("\r\n\r\n") + 4));
    if (!parsed.ok) {
        send_response(client_fd, error_response(400, "Bad Request", "Malformed request\n", true));
        close_client();
        return;
    }
    const bool is_head = parsed.request.method == "HEAD";
    http::Response response;
    if (parsed.request.method != "GET" && !is_head) {
        response = error_response(405, "Method Not Allowed", "Only GET and HEAD are supported\n", true);
        response.headers.emplace("Allow", "GET, HEAD");
    } else {
        const std::string path = http::safe_path_from_target(parsed.request.target);
        if (path.empty()) response = error_response(403, "Forbidden", "Forbidden\n", !is_head);
        else if (path == "/health") response = {200, "OK", "ok\n", "text/plain; charset=utf-8", !is_head, {}};
        else if (path == "/stats") response = {200, "OK", stats_body(), "application/json; charset=utf-8", !is_head, {}};
        else {
            std::filesystem::path file = document_root_ / path.substr(1);
            if (path == "/") file /= "index.html";
            const auto resolved = std::filesystem::weakly_canonical(file);
            if (!is_within(document_root_, resolved) || !std::filesystem::is_regular_file(resolved))
                response = error_response(404, "Not Found", "Not found\n", !is_head);
            else {
                std::ifstream input(resolved, std::ios::binary);
                std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                response = {200, "OK", std::move(body), http::mime_type_for_path(resolved.string()), !is_head, {}};
            }
        }
    }
    send_response(client_fd, response);
    close_client();
}
