# MiniNet

MiniNet is a small Linux TCP/HTTP server written from scratch in C++17. It uses POSIX sockets and a fixed-size thread pool, without external web frameworks.

## Build (Linux)

```bash
cmake -S . -B build
cmake --build build
./build/mininet 8080 public 4
```

Arguments are `port`, `document-root`, and `worker-count`; all are optional. The server binds to `0.0.0.0` and serves `index.html` for `/`.

## Try it

```bash
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/hello.txt
```

Only `GET` and `HEAD` are supported. Requests that attempt directory traversal, exceed 16 KiB of headers, or request unsupported methods receive appropriate HTTP errors.

## Layout

- `src/main.cpp` — process startup and signal handling
- `src/http_server.cpp` — TCP listener, HTTP parsing, and response generation
- `src/thread_pool.cpp` — worker queue implementation
- `public/` — example static content

This is a learning server rather than a production-hardened web server: it does not yet implement keep-alive, TLS, chunked transfer encoding, CGI, or an event-driven I/O loop.
