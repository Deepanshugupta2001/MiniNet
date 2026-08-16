#include "http.hpp"
#include "http_server.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string request(std::uint16_t port, const std::string& value) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        throw std::runtime_error("connect failed");
    }
    send(fd, value.data(), value.size(), 0);
    shutdown(fd, SHUT_WR);
    std::string response;
    char buffer[1024];
    for (;;) {
        const auto received = recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        response.append(buffer, static_cast<std::size_t>(received));
    }
    close(fd);
    return response;
}

void expect_status(const std::string& response, int status) {
    expect(response.rfind("HTTP/1.1 " + std::to_string(status) + " ", 0) == 0, "unexpected response: " + response);
}

void unit_tests() {
    const auto parsed = http::parse_request("GET /hello.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    expect(parsed.ok && parsed.request.method == "GET" && parsed.request.headers.at("Host") == "localhost", "valid request parsing failed");
    expect(!http::parse_request("GET / HTTP/2.0\r\n\r\n").ok, "unsupported HTTP version accepted");
    expect(!http::parse_request("GET / HTTP/1.1\n\n").ok, "LF-only request accepted");
    expect(http::safe_path_from_target("/assets/site.css?x=1") == "/assets/site.css", "safe target rejected");
    expect(http::safe_path_from_target("/%2e%2e/secret").empty(), "encoded traversal accepted");
    expect(http::safe_path_from_target("/..\\secret").empty(), "backslash traversal accepted");
    const auto serialized = http::serialize_response({200, "OK", "abc", "text/plain", false, {{"X-Test", "yes"}}});
    expect(serialized.find("Content-Length: 3\r\n") != std::string::npos && serialized.find("X-Test: yes\r\n") != std::string::npos,
           "response headers were not serialized");
    expect(serialized.substr(serialized.find("\r\n\r\n") + 4).empty(), "HEAD response serialized a body");
}

void integration_tests() {
    const auto root = std::filesystem::temp_directory_path() / ("mininet-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    { std::ofstream(root / "index.html") << "home"; std::ofstream(root / "hello.txt") << "hello"; }
    HttpServer server(0, root, 4);
    std::thread server_thread([&] { server.run(); });
    for (int attempt = 0; attempt < 100 && server.port() == 0; ++attempt) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(server.port() != 0, "server did not bind");
    const auto port = server.port();
    expect_status(request(port, "GET /hello.txt HTTP/1.1\r\nHost: x\r\n\r\n"), 200);
    const auto head = request(port, "HEAD /hello.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    expect_status(head, 200);
    expect(head.substr(head.find("\r\n\r\n") + 4).empty(), "HEAD returned a body");
    expect_status(request(port, "GET /missing HTTP/1.1\r\nHost: x\r\n\r\n"), 404);
    expect_status(request(port, "GET /%2e%2e/secret HTTP/1.1\r\nHost: x\r\n\r\n"), 403);
    expect_status(request(port, "BROKEN\r\n\r\n"), 400);
    expect_status(request(port, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n"), 200);
    expect_status(request(port, "POST /hello.txt HTTP/1.1\r\nHost: x\r\n\r\n"), 405);
    expect_status(request(port, "GET / HTTP/1.1\r\nHost: x\r\nX-Large: " + std::string(16 * 1024, 'a') + "\r\n\r\n"), 431);
    const auto stats = request(port, "GET /stats HTTP/1.1\r\nHost: x\r\n\r\n");
    expect_status(stats, 200);
    expect(stats.find("\"requests\":") != std::string::npos && stats.find("\"errors\":") != std::string::npos &&
               stats.find("\"active_connections\":") != std::string::npos,
           "stats response is missing counters");
    std::vector<std::thread> clients;
    for (int i = 0; i < 12; ++i) clients.emplace_back([&] { expect_status(request(port, "GET /hello.txt HTTP/1.1\r\nHost: x\r\n\r\n"), 200); });
    for (auto& client : clients) client.join();
    server.stop();
    server_thread.join();
    std::filesystem::remove_all(root);
}
} // namespace

int main() {
    try {
        unit_tests();
        integration_tests();
        std::cout << "All MiniNet tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
