#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <vector>

#define SOCKET_PATH "/tmp/hll_socket.sock"

// Helper: read exactly n bytes
bool recv_all(int sock, void* buf, size_t len) {
    char* ptr = (char*)buf;
    size_t received = 0;
    while (received < len) {
        ssize_t r = read(sock, ptr + received, len - received);
        if (r <= 0) return false;
        received += r;
    }
    return true;
}

int main() {
    // Create socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    // JSON payload
    std::string json = R"({"key":"value","count":42})";
    uint32_t len = htonl(json.size());

    // Send length-prefixed JSON
    write(sock, &len, sizeof(len));
    write(sock, json.data(), json.size());

    // Read response length
    uint32_t resp_len;
    if (!recv_all(sock, &resp_len, sizeof(resp_len))) { perror("read"); return 1; }
    resp_len = ntohl(resp_len);

    // Read response JSON
    std::vector<char> buffer(resp_len + 1);
    if (!recv_all(sock, buffer.data(), resp_len)) { perror("read"); return 1; }
    buffer[resp_len] = '\0';

    std::cout << "Response: " << buffer.data() << "\n";

    close(sock);
    return 0;
}
