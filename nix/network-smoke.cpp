#include <borealis/http.hpp>
#include <borealis/ws.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

int main(int argc, char* argv[]) {
    using namespace std::chrono_literals;
    if (!borealis::http::available() || !borealis::ws::available()) {
        std::fprintf(stderr, "HTTP or WebSocket backend unavailable\n");
        return 1;
    }
    std::printf("HTTP backend: %s; WebSocket backend available\n", borealis::http::backend_name());
    if (argc < 2) {
        return 0;
    }
    auto request = borealis::http::start({.url = argv[1], .totalTimeout = 20s});
    const auto deadline = std::chrono::steady_clock::now() + 25s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = request.try_take()) {
            const bool success = result->error == borealis::http::Error::None &&
                                 result->response.statusCode == 200 && !result->response.body.empty();
            std::printf("HTTPS status: %d; bytes: %zu; error: %s\n",
                        result->response.statusCode, result->response.body.size(), result->message.c_str());
            borealis::shutdown();
            return success ? 0 : 1;
        }
        std::this_thread::sleep_for(10ms);
    }
    request.cancel();
    borealis::shutdown();
    return 1;
}
