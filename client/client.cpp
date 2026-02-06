#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>

#define PORT 8080
#define BUFFER_SIZE 1024

// Convert command to uppercase
std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

void upload_file(int sock, const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    char buffer[BUFFER_SIZE];

    if (!file) {
        std::cout << "File not found on client side\n";
        return;
    }

    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        send(sock, buffer, file.gcount(), 0);
    }

    strcpy(buffer, "EOF");
    send(sock, buffer, BUFFER_SIZE, 0);
    file.close();

    std::cout << "Upload completed\n";
}

void download_file(int sock, const std::string &filename) {
    std::ofstream file(filename, std::ios::binary);
    char buffer[BUFFER_SIZE];
    int bytes;

    while ((bytes = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (strcmp(buffer, "EOF") == 0)
            break;
        file.write(buffer, bytes);
    }

    file.close();
    std::cout << "Download completed\n";
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Connect to server
    connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    std::cout << "Connected to server.\n";

    while (true) {
        std::cout << "\nEnter command: ";
        std::string command;
        getline(std::cin, command);

        std::string upper_command = to_upper(command);
        send(sock, upper_command.c_str(), BUFFER_SIZE, 0);

        if (upper_command == "LIST") {
            memset(buffer, 0, BUFFER_SIZE);
            recv(sock, buffer, BUFFER_SIZE, 0);
            std::cout << buffer;
        }
        else if (upper_command.rfind("UPLOAD", 0) == 0) {
            std::string filename = command.substr(7);

            // Check file existence on client BEFORE notifying server
            std::ifstream test(filename);
            if (!test) {
                std::cout << "File not found on client side\n";
                continue;   // do NOT send UPLOAD command to server
            }
            test.close();

            // Notify server only if file exists
            send(sock, upper_command.c_str(), BUFFER_SIZE, 0);
            upload_file(sock, filename);
        }

        else if (upper_command.rfind("DOWNLOAD", 0) == 0) {
            download_file(sock, command.substr(9));
        }
        else if (upper_command == "EXIT") {
            std::cout << "Connection closed.\n";
            break;
        }
        else {
            std::cout << "Invalid command\n";
        }
    }

    close(sock);
    return 0;
}
