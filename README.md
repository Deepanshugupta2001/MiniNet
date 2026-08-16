# MiniNet

MiniNet is a small C++17 static HTTP server built with POSIX blocking sockets and a bounded, fixed-size thread pool. It is intended for learning and local use, not production deployment.

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/mininet 8080 public 4
```

Arguments are `port`, `document-root`, and `worker-count`; all are optional. The server binds to `0.0.0.0`. It runs on Linux and macOS with CMake and a C++17 compiler.

## Architecture

The accept loop owns the listening socket and submits each accepted client to the bounded thread pool. A worker reads one request, parses it through `src/http.cpp`, routes it, serves a static file or health response, serializes an HTTP response, and closes the connection.

`HttpServer::stop()` stops accepting connections, shuts down active client sockets, drains queued handlers (which promptly close after shutdown), and joins all worker threads. `SIGINT` and `SIGTERM` invoke that shutdown path. The server deliberately uses blocking sockets plus a bounded thread pool; it is not an event-driven server.

The HTTP module defines `HttpRequest` and `HttpResponse` types and keeps parsing, response serialization, target validation, and MIME resolution separate from socket I/O. The server layer handles routing, static file access, metrics, and sockets.

## Supported HTTP behavior

- `GET` and `HEAD` over HTTP/1.0 or HTTP/1.1; one request per connection and `Connection: close` responses.
- `/` serves `index.html`; other paths serve regular files under the document root.
- `/health` returns `200 OK` with `ok`.
- `/stats` returns JSON counters for handled requests, HTTP error responses, and active connections.
- Missing files return `404`; unsafe targets return `403`; unsupported methods return `405` with `Allow: GET, HEAD`; malformed request heads return `400`; oversized request headers return `431`.
- `HEAD` has the same headers and `Content-Length` as `GET`, without the body.

## Security limits and constraints

Request headers are limited to 16 KiB. Targets must use origin form and reject backslashes, control characters, `..` path components, and percent-encoded traversal. Canonical-path containment also prevents symlinks from escaping the document root. Socket reads and writes retry on `EINTR`; partial writes are retried and failed sends are reported to standard error.

MiniNet has no TLS, keep-alive, chunked transfer encoding, request bodies, virtual hosts, CGI, HTTP/2+, timeouts, or rate limiting. A slow client can occupy one worker because socket I/O is blocking.

## Tests

`mininet_tests` includes parser, response serialization, and traversal-protection unit tests. Its socket integration coverage includes GET, HEAD, 403, 404, 405, 431, `/health`, `/stats`, malformed requests, and concurrent clients. CTest runs the suite.

## Sanitizers

Run AddressSanitizer and UndefinedBehaviorSanitizer builds on GCC or Clang:

```bash
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DMININET_ENABLE_ASAN=ON -DMININET_ENABLE_UBSAN=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

GitHub Actions runs regular Linux build/test and sanitizer build/test workflows on pushes and pull requests.

## Repeatable load test

Start the server in one terminal, then run this dependency-free Python client in another:

```bash
python3 tools/load_test.py --url http://127.0.0.1:8080/health --requests 1000 --concurrency 20
```

Sample benchmark on this project's WSL/Linux development environment (results vary by hardware and load):

| Requests | Concurrency | Failures | Requests/sec | p95 latency |
| ---: | ---: | ---: | ---: | ---: |
| 500 | 20 | 0 | 2,181.7 | 12.55 ms |
