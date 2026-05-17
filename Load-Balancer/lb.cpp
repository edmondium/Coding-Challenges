#include <iostream>
#include <vector>
#include <expected>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <ranges>
#include <optional>
#include <string_view>

enum class lb_error { socket_err, bind_err, listen_err, connect_err };

struct backend {
    std::string host;
    int port;
    bool alive = true; 
};

std::vector<backend> backends = {
    {"127.0.0.1", 8081},
    {"127.0.0.1", 8082},
    {"127.0.0.1", 8083}
};

std::shared_mutex backend_mtx; 
std::atomic<size_t> next_backend_idx{0};

auto check_health(const backend& be) -> bool {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct timeval tv{.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(be.port)};
    inet_pton(AF_INET, be.host.c_str(), &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    std::string req = "GET / HTTP/1.1\r\nHost: " + be.host + "\r\nConnection: close\r\n\r\n";
    [[maybe_unused]] auto s = send(fd, req.c_str(), req.size(), 0);

    char buffer[16];
    ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
    close(fd);

    return n > 9 && std::string(buffer, 12).find("200") != std::string::npos;
}

void health_checker_loop(int interval_seconds) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        
        auto statuses = backends 
                      | std::views::transform([](const auto& be) { return check_health(be); })
                      | std::ranges::to<std::vector<bool>>();
        {
            std::unique_lock lock(backend_mtx);
            std::ranges::for_each(std::views::iota(0u, backends.size()), [&](size_t i) {
                backends[i].alive = statuses[i];
            });
        }
    }
}

auto create_lb(int port) -> std::expected<int, lb_error> {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return std::unexpected(lb_error::socket_err);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(port), .sin_addr = {INADDR_ANY}};
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) return std::unexpected(lb_error::bind_err);
    if (listen(fd, 100) < 0) return std::unexpected(lb_error::listen_err);
    return fd;
}

auto connect_to_backend(const backend& be) -> std::expected<int, lb_error> {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return std::unexpected(lb_error::socket_err);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(be.port)};
    inet_pton(AF_INET, be.host.c_str(), &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return std::unexpected(lb_error::connect_err);
    }
    return fd;
}

auto select_backend() -> std::optional<backend> {
    std::shared_lock lock(backend_mtx);
    size_t start_idx = next_backend_idx.fetch_add(1, std::memory_order_relaxed);
    
    auto indices = std::views::iota(0u, backends.size())
                 | std::views::transform([=](size_t i) { return (start_idx + i) % backends.size(); })
                 | std::views::filter([](size_t idx) { return backends[idx].alive; });

    if (auto it = indices.begin(); it != indices.end()) {
        return backends[*it];
    }
    return std::nullopt;
}

auto transform_buffer(char* buffer, ssize_t size) -> void {
    #pragma acc parallel loop copy(buffer[0:size])
    for (ssize_t i = 0; i < size; ++i) {
        if (buffer[i] >= 'a' && buffer[i] <= 'z') {
            buffer[i] -= 32;
        }
    }
}

auto process_client(int client_fd, sockaddr_in client_addr) -> void {
    char t_buffer[4096];
    ssize_t n = read(client_fd, t_buffer, sizeof(t_buffer));
    
    if (n > 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        
        std::cout << "Received request from " << ip_str << "\n" 
                  << std::string_view(t_buffer, n) << std::flush;

        auto proxy_chain = select_backend()
            .and_then([&](const backend& target) -> std::optional<int> {
                auto conn = connect_to_backend(target);
                return conn ? std::optional<int>(*conn) : std::nullopt;
            })
            .and_then([&](int be_fd) -> std::optional<int> {
                [[maybe_unused]] auto s1 = write(be_fd, t_buffer, n);
                
                std::vector<char> full_resp;
                char chunk[4096];
                ssize_t chunk_n = 0;
                
                while ((chunk_n = read(be_fd, chunk, sizeof(chunk))) > 0) {
                    full_resp.insert(full_resp.end(), chunk, chunk + chunk_n);
                }

                if (!full_resp.empty()) {
                    std::string_view resp_view(full_resp.data(), full_resp.size());
                    size_t header_end = resp_view.find("\r\n\r\n");
                    
                    if (header_end != std::string_view::npos) {
                        size_t body_offset = header_end + 4;
                        char* body_ptr = full_resp.data() + body_offset;
                        ssize_t body_len = full_resp.size() - body_offset;
                        
                        if (body_len > 0) {
                            transform_buffer(body_ptr, body_len);
                        }
                    } else {
                        transform_buffer(full_resp.data(), full_resp.size());
                    }

                    [[maybe_unused]] auto s2 = write(client_fd, full_resp.data(), full_resp.size());
                }
                close(be_fd);
                return 1;
            });

        if (!proxy_chain.has_value()) {
            std::string err_resp = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            [[maybe_unused]] auto s3 = write(client_fd, err_resp.c_str(), err_resp.size());
        }
    }
    close(client_fd);
}

auto main(int argc, char* argv[]) -> int {
    int interval = (argc > 1) ? std::stoi(argv[1]) : 10;
    auto lb_fd = create_lb(80).value_or(-1); 
    if (lb_fd == -1) return 1;

    std::jthread health_thread(health_checker_loop, interval);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(lb_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        std::thread(process_client, client_fd, client_addr).detach();
    }
    return 0;
}