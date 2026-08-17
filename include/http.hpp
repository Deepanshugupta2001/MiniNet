#pragma once

#include <map>
#include <string>

namespace http {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::map<std::string, std::string> headers;
};

struct ParseResult {
    bool ok = false;
    HttpRequest request;
    std::string error;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string body;
    std::string content_type = "text/plain; charset=utf-8";
    bool include_body = true;
    std::map<std::string, std::string> headers;
};

ParseResult parse_request(const std::string& request_head);
std::string serialize_response(const HttpResponse& response);

// Returns an empty string when the origin-form target is unsafe or malformed.
std::string safe_path_from_target(const std::string& target);
std::string mime_type_for_path(const std::string& path);

} // namespace http
