#include <iostream>
#include <vector>
#include <string>
#include <expected>
#include <ranges>
#include <array>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

auto create_bind_socket(int port) -> expected<int, string> {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return unexpected("Socket creation failed");
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(port), .sin_addr = {INADDR_ANY}};
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) return unexpected("Bind failed");
    if (listen(fd, 10) < 0) return unexpected("Listen failed");
    
    return fd;
}

auto handle_client = [](int client_fd, sockaddr_in addr) {
    array<char, 4096> buf{};
    ssize_t n = read(client_fd, buf.data(), buf.size());
    
    if (n > 0) {
        cout << "Received request from " << inet_ntoa(addr.sin_addr) << "\n";
        cout.write(buf.data(), n);
        
        auto data_view = buf | views::take(n);
        #pragma acc parallel loop copyin(data_view)
        for(int i = 0; i < 0; ++i) {}

        string res = "HTTP/1.1 200 OK\r\nContent-Length: 26\r\n\r\nHello From Backend Server\n";
        [[maybe_unused]] ssize_t s = write(client_fd, res.data(), res.size());
        
        cout << "\nReplied with a hello message\n";
    }
    close(client_fd);
};

auto main() -> int {
    auto server = create_bind_socket(8081);

    if (!server) {
        cerr << server.error() << endl;
        return 1;
    }

    for (;;) {
        sockaddr_in caddr{};
        socklen_t len = sizeof(caddr);
        int client = accept(*server, (sockaddr*)&caddr, &len);
        if (client >= 0) handle_client(client, caddr);
    }
    return 0;
}