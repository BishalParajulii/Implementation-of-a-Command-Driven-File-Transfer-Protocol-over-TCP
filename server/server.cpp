#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

constexpr int kPort = 8080;
constexpr int kBacklog = 5;
constexpr std::size_t kBufferSize = 1024;

bool send_all(int sock, const char* data, std::size_t length) {
    std::size_t sent = 0;
    while (sent < length) {
        const ssize_t written = send(sock, data + sent, length - sent, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

bool recv_all(int sock, char* data, std::size_t length) {
    std::size_t received = 0;
    while (received < length) {
        const ssize_t bytes = recv(sock, data + received, length - received, 0);
        if (bytes == 0) {
            return false;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        received += static_cast<std::size_t>(bytes);
    }
    return true;
}

bool send_u64(int sock, std::uint64_t value) {
    char bytes[8];
    for (int i = 7; i >= 0; --i) {
        bytes[i] = static_cast<char>(value & 0xFFu);
        value >>= 8;
    }
    return send_all(sock, bytes, sizeof(bytes));
}

bool recv_u64(int sock, std::uint64_t& value) {
    char bytes[8];
    if (!recv_all(sock, bytes, sizeof(bytes))) {
        return false;
    }

    value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<unsigned char>(bytes[i]);
    }
    return true;
}

void list_files(int client_sock) {
    DIR* dir = opendir(".");
    if (dir == nullptr) {
        const char* msg = "Unable to open directory\n";
        send_all(client_sock, msg, std::strlen(msg));
        return;
    }

    std::string response;
    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        const std::string line = std::string(entry->d_name) + '\n';
        if (response.size() + line.size() >= kBufferSize - 32) {
            response += "... output truncated ...\n";
            break;
        }
        response += line;
    }
    closedir(dir);

    if (response.empty()) {
        response = "No files found\n";
    }

    send_all(client_sock, response.c_str(), response.size());
}

void receive_file(int client_sock, const std::string& filename) {
    std::uint64_t file_size = 0;
    if (!recv_u64(client_sock, file_size)) {
        std::cerr << "Failed to receive upload size for: " << filename << '\n';
        return;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for upload: " << filename << '\n';
        return;
    }

    char buffer[kBufferSize];
    std::uint64_t remaining = file_size;
    while (remaining > 0) {
        const std::size_t chunk = remaining > kBufferSize
            ? kBufferSize
            : static_cast<std::size_t>(remaining);

        if (!recv_all(client_sock, buffer, chunk)) {
            std::cerr << "Upload interrupted for: " << filename << '\n';
            return;
        }

        file.write(buffer, static_cast<std::streamsize>(chunk));
        if (!file) {
            std::cerr << "Failed to write uploaded data to file: " << filename << '\n';
            return;
        }
        remaining -= chunk;
    }
}

void send_file(int client_sock, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        char error_packet[kBufferSize] = {0};
        std::strncpy(error_packet, "ERROR", sizeof(error_packet) - 1);
        send_all(client_sock, error_packet, sizeof(error_packet));
        return;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff stream_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (stream_size < 0) {
        std::cerr << "Failed to determine file size for: " << filename << '\n';
        return;
    }

    char ok_packet[kBufferSize] = {0};
    std::strncpy(ok_packet, "OK", sizeof(ok_packet) - 1);
    if (!send_all(client_sock, ok_packet, sizeof(ok_packet))) {
        std::cerr << "Failed to send download status for: " << filename << '\n';
        return;
    }

    const std::uint64_t file_size = static_cast<std::uint64_t>(stream_size);
    if (!send_u64(client_sock, file_size)) {
        std::cerr << "Failed to send file size for: " << filename << '\n';
        return;
    }

    char buffer[kBufferSize];
    while (file) {
        file.read(buffer, sizeof(buffer));
        const std::streamsize count = file.gcount();
        if (count > 0 && !send_all(client_sock, buffer, static_cast<std::size_t>(count))) {
            std::cerr << "send failed while sending file: " << filename << '\n';
            return;
        }
    }
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

}  // namespace

int main() {
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, kBacklog) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    std::cout << "Server listening on port " << kPort << "...\n";

    const int client_sock = accept(server_fd, nullptr, nullptr);
    if (client_sock < 0) {
        std::cerr << "accept failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }
    std::cout << "Client connected.\n";

    char buffer[kBufferSize];
    while (true) {
        std::memset(buffer, 0, sizeof(buffer));
        const ssize_t bytes = recv(client_sock, buffer, sizeof(buffer), 0);
        if (bytes == 0) {
            std::cout << "Client disconnected.\n";
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "recv failed: " << std::strerror(errno) << '\n';
            break;
        }

        std::string command(buffer);

        if (command == "LIST") {
            list_files(client_sock);
        } else if (starts_with(command, "UPLOAD ")) {
            const std::string filename = command.substr(7);
            receive_file(client_sock, filename);
        } else if (starts_with(command, "DOWNLOAD ")) {
            const std::string filename = command.substr(9);
            send_file(client_sock, filename);
        } else if (command == "EXIT") {
            break;
        } else if (!command.empty()) {
            std::cerr << "Unknown command: " << command << '\n';
        }
    }

    close(client_sock);
    close(server_fd);
    return 0;
}
