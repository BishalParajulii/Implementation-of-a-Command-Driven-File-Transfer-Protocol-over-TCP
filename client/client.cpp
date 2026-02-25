#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

constexpr int kPort = 8080;
constexpr std::size_t kBufferSize = 1024;
constexpr const char* kServerIp = "127.0.0.1";

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

bool send_command(int sock, const std::string& command) {
    char packet[kBufferSize] = {0};
    std::strncpy(packet, command.c_str(), sizeof(packet) - 1);
    return send_all(sock, packet, sizeof(packet));
}

bool is_marker(const char* buffer, const char* marker) {
    return std::strcmp(buffer, marker) == 0;
}

std::string to_upper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

void upload_file(int sock, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "File not found on client side\n";
        return;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff stream_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (stream_size < 0) {
        std::cerr << "Upload failed: unable to determine file size\n";
        return;
    }

    const std::uint64_t file_size = static_cast<std::uint64_t>(stream_size);
    if (!send_u64(sock, file_size)) {
        std::cerr << "Upload failed: unable to send file size\n";
        return;
    }

    char buffer[kBufferSize];
    while (file) {
        file.read(buffer, sizeof(buffer));
        const std::streamsize count = file.gcount();
        if (count > 0 && !send_all(sock, buffer, static_cast<std::size_t>(count))) {
            std::cerr << "Upload failed: send error\n";
            return;
        }
    }

    std::cout << "Upload completed\n";
}

void download_file(int sock, const std::string& filename) {
    char status[kBufferSize] = {0};
    if (!recv_all(sock, status, sizeof(status))) {
        std::cerr << "Download failed: missing status from server\n";
        return;
    }

    if (is_marker(status, "ERROR")) {
        std::cout << "File not found on server side\n";
        return;
    }
    if (!is_marker(status, "OK")) {
        std::cerr << "Download failed: invalid status from server\n";
        return;
    }

    std::uint64_t file_size = 0;
    if (!recv_u64(sock, file_size)) {
        std::cerr << "Download failed: missing file size\n";
        return;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Unable to create local file: " << filename << '\n';
        return;
    }

    char buffer[kBufferSize];
    std::uint64_t remaining = file_size;
    while (remaining > 0) {
        const std::size_t chunk = remaining > kBufferSize
            ? kBufferSize
            : static_cast<std::size_t>(remaining);

        if (!recv_all(sock, buffer, chunk)) {
            std::cerr << "Download failed: connection interrupted\n";
            file.close();
            std::remove(filename.c_str());
            return;
        }

        file.write(buffer, static_cast<std::streamsize>(chunk));
        if (!file) {
            std::cerr << "Download failed: unable to write file\n";
            return;
        }
        remaining -= chunk;
    }

    std::cout << "Download completed\n";
}

}  // namespace

int main() {
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    if (inet_pton(AF_INET, kServerIp, &server_addr.sin_addr) != 1) {
        std::cerr << "Invalid server address\n";
        close(sock);
        return 1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "connect failed: " << std::strerror(errno) << '\n';
        close(sock);
        return 1;
    }

    std::cout << "Connected to server.\n";

    char buffer[kBufferSize];
    while (true) {
        std::cout << "\nEnter command: ";
        std::string input;
        if (!std::getline(std::cin, input)) {
            std::cout << "\nInput closed. Exiting.\n";
            break;
        }
        if (input.empty()) {
            continue;
        }

        const std::size_t separator = input.find(' ');
        const std::string verb = to_upper(separator == std::string::npos ? input : input.substr(0, separator));
        const std::string argument = separator == std::string::npos ? "" : input.substr(separator + 1);

        if (verb == "LIST") {
            if (!send_command(sock, "LIST")) {
                std::cerr << "Failed to send LIST command\n";
                break;
            }

            std::memset(buffer, 0, sizeof(buffer));
            const ssize_t bytes = recv(sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) {
                std::cerr << "Failed to receive LIST response\n";
                break;
            }
            std::cout.write(buffer, bytes);
            continue;
        }

        if (verb == "UPLOAD") {
            if (argument.empty()) {
                std::cout << "Usage: UPLOAD <filename>\n";
                continue;
            }

            std::ifstream test(argument, std::ios::binary);
            if (!test.is_open()) {
                std::cout << "File not found on client side\n";
                continue;
            }
            test.close();

            if (!send_command(sock, "UPLOAD " + argument)) {
                std::cerr << "Failed to send UPLOAD command\n";
                break;
            }
            upload_file(sock, argument);
            continue;
        }

        if (verb == "DOWNLOAD") {
            if (argument.empty()) {
                std::cout << "Usage: DOWNLOAD <filename>\n";
                continue;
            }

            if (!send_command(sock, "DOWNLOAD " + argument)) {
                std::cerr << "Failed to send DOWNLOAD command\n";
                break;
            }
            download_file(sock, argument);
            continue;
        }

        if (verb == "EXIT") {
            send_command(sock, "EXIT");
            std::cout << "Connection closed.\n";
            break;
        }

        std::cout << "Invalid command\n";
    }

    close(sock);
    return 0;
}
