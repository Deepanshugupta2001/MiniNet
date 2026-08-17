#include "http.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace http {
namespace {
bool has_invalid_characters(const std::string& value) {
    for (unsigned char character : value) {
        if (character < 0x20 || character == 0x7f) return true;
    }
    return false;
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}
} // namespace

ParseResult parse_request(const std::string& request_head) {
    ParseResult result;
    if (request_head.size() < 4 || request_head.compare(request_head.size() - 4, 4, "\r\n\r\n") != 0) {
        result.error = "request headers must end with CRLF CRLF";
        return result;
    }

    const std::size_t first_end = request_head.find("\r\n");
    if (first_end == std::string::npos || first_end == 0) {
        result.error = "missing request line";
        return result;
    }
    const std::string request_line = request_head.substr(0, first_end);
    std::istringstream line(request_line);
    std::string extra;
    if (!(line >> result.request.method >> result.request.target >> result.request.version) || (line >> extra) ||
        (result.request.version != "HTTP/1.0" && result.request.version != "HTTP/1.1")) {
        result.error = "malformed request line";
        return result;
    }

    std::size_t position = first_end + 2;
    while (position < request_head.size() - 2) {
        const std::size_t end = request_head.find("\r\n", position);
        if (end == std::string::npos) {
            result.error = "malformed header";
            return result;
        }
        if (end == position) break;
        const std::string header = request_head.substr(position, end - position);
        const std::size_t colon = header.find(':');
        if (colon == std::string::npos || colon == 0 || has_invalid_characters(header)) {
            result.error = "malformed header";
            return result;
        }
        std::string value = header.substr(colon + 1);
        const std::size_t first = value.find_first_not_of(" \t");
        const std::size_t last = value.find_last_not_of(" \t");
        result.request.headers.emplace(header.substr(0, colon),
                                       first == std::string::npos ? "" : value.substr(first, last - first + 1));
        position = end + 2;
    }
    result.ok = true;
    return result;
}

std::string serialize_response(const HttpResponse& response) {
    std::ostringstream output;
    output << "HTTP/1.1 " << response.status << ' ' << response.reason << "\r\n"
           << "Content-Type: " << response.content_type << "\r\n"
           << "Content-Length: " << response.body.size() << "\r\n"
           << "Connection: close\r\n";
    for (const auto& [name, value] : response.headers) output << name << ": " << value << "\r\n";
    output << "\r\n";
    if (response.include_body) output << response.body;
    return output.str();
}

std::string safe_path_from_target(const std::string& target) {
    const std::size_t query = target.find('?');
    const std::string encoded_path = target.substr(0, query);
    if (encoded_path.empty() || encoded_path.front() != '/') return {};

    std::string path;
    for (std::size_t index = 0; index < encoded_path.size(); ++index) {
        char character = encoded_path[index];
        if (character == '%') {
            if (index + 2 >= encoded_path.size()) return {};
            const int high = hex_value(encoded_path[index + 1]);
            const int low = hex_value(encoded_path[index + 2]);
            if (high < 0 || low < 0) return {};
            character = static_cast<char>((high << 4) | low);
            index += 2;
        }
        if (character == '\\' || character == '\0' || static_cast<unsigned char>(character) < 0x20) return {};
        path += character;
    }

    std::istringstream components(path);
    std::string component;
    while (std::getline(components, component, '/')) {
        if (component == "..") return {};
    }
    return path;
}

std::string mime_type_for_path(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::string extension = dot == std::string::npos ? "" : path.substr(dot);
    if (extension == ".html" || extension == ".htm") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".txt") return "text/plain; charset=utf-8";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    return "application/octet-stream";
}

} // namespace http
