#include "http_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>

namespace {
constexpr std::size_t kMaxHeaderBytes = 16 * 1024;

void send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto result = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result <= 0) return;
        sent += static_cast<std::size_t>(result);
    }
}

std::string mime_type(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    return "application/octet-stream";
}

void respond(int fd, int code, const std::string& reason, const std::string& body,
             const std::string& content_type = "text/plain; charset=utf-8", bool include_body = true) {
    std::ostringstream response;
    response << "HTTP/1.1 " << code << ' ' << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n";
    if (include_body) response << body;
    send_all(fd, response.str());
}

bool is_safe_target(const std::string& target) {
    return !target.empty() && target.front() == '/' && target.find("..") == std::string::npos &&
           target.find('\\') == std::string::npos;
}
} // namespace

HttpServer::HttpServer(std::uint16_t port, std::filesystem::path document_root, std::size_t workers)
    : port_(port), document_root_(std::filesystem::canonical(std::move(document_root))), pool_(workers) {}

HttpServer::~HttpServer() { stop(); }

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
        close(listen_fd_); listen_fd_ = -1;
        throw std::runtime_error(error);
    }
    running_ = true;
    std::cout << "MiniNet listening on http://0.0.0.0:" << port_ << '\n';

    while (running_) {
        const int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!running_) break;
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            continue;
        }
        pool_.submit([this, client_fd] { handle_client(client_fd); });
    }
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::handle_client(int client_fd) {
    std::string request;
    char buffer[4096];
    while (request.size() < kMaxHeaderBytes && request.find("\r\n\r\n") == std::string::npos) {
        const auto received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) { close(client_fd); return; }
        request.append(buffer, static_cast<std::size_t>(received));
    }
    if (request.size() >= kMaxHeaderBytes) { respond(client_fd, 431, "Request Header Fields Too Large", "Headers too large\n"); close(client_fd); return; }

    std::istringstream stream(request);
    std::string method, target, version;
    stream >> method >> target >> version;
    if (version.rfind("HTTP/", 0) != 0) respond(client_fd, 400, "Bad Request", "Malformed request\n");
    else if (method != "GET" && method != "HEAD") respond(client_fd, 405, "Method Not Allowed", "Only GET and HEAD are supported\n");
    else if (!is_safe_target(target)) respond(client_fd, 403, "Forbidden", "Forbidden\n");
    else {
        const auto query_at = target.find('?');
        if (query_at != std::string::npos) target.erase(query_at);
        std::filesystem::path file = document_root_ / target.substr(1);
        if (target == "/") file /= "index.html";
        if (!std::filesystem::is_regular_file(file)) respond(client_fd, 404, "Not Found", "Not found\n");
        else {
            std::ifstream input(file, std::ios::binary);
            std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            respond(client_fd, 200, "OK", body, mime_type(file), method == "GET");
        }
    }
    close(client_fd);
}
