#!/usr/bin/env python3
"""Small dependency-free HTTP load test for MiniNet."""

import argparse
import concurrent.futures
import http.client
import time
import urllib.parse


def one_request(url):
    target = urllib.parse.urlparse(url)
    connection = http.client.HTTPConnection(target.hostname, target.port or 80, timeout=5)
    started = time.perf_counter()
    try:
        connection.request("GET", target.path or "/")
        response = connection.getresponse()
        response.read()
        return response.status == 200, time.perf_counter() - started
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description="Run concurrent GET requests against MiniNet.")
    parser.add_argument("--url", default="http://127.0.0.1:8080/health")
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=20)
    args = parser.parse_args()
    if args.requests < 1 or args.concurrency < 1:
        parser.error("--requests and --concurrency must be positive")

    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        results = list(executor.map(one_request, [args.url] * args.requests))
    elapsed = time.perf_counter() - started
    successes = sum(ok for ok, _ in results)
    failures = args.requests - successes
    latencies = sorted(latency for _, latency in results)
    percentile_95 = latencies[max(0, int(len(latencies) * 0.95) - 1)] * 1000
    print(f"requests={args.requests} successes={successes} failures={failures}")
    print(f"elapsed_seconds={elapsed:.3f} requests_per_second={args.requests / elapsed:.1f} p95_ms={percentile_95:.2f}")
    raise SystemExit(0 if failures == 0 else 1)


if __name__ == "__main__":
    main()
