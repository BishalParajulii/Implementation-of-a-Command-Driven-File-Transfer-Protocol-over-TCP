#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void list_files(int client_sock) {
    DIR *dir;
    struct dirent *entry;
    dir = opendir(".");

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    if (dir == nullptr) {
        strcpy(buffer, "Unable to open directory\n");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }

    while ((entry = readdir(dir)) != nullptr) {
        strcat(buffer, entry->d_name);
        strcat(buffer, "\n");
    }

    closedir(dir);
    send(client_sock, buffer, strlen(buffer), 0);
}

void receive_file(int client_sock, const std::string &filename) {
    std::ofstream file(filename, std::ios::binary);
    char buffer[BUFFER_SIZE];
    int bytes;

    while ((bytes = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (strcmp(buffer, "EOF") == 0)
            break;
        file.write(buffer, bytes);
    }

    file.close();
}

void send_file(int client_sock, const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    char buffer[BUFFER_SIZE];

    if (!file) {
        strcpy(buffer, "ERROR");
        send(client_sock, buffer, BUFFER_SIZE, 0);
        return;
    }

    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        send(client_sock, buffer, file.gcount(), 0);
    }

    strcpy(buffer, "EOF");
    send(client_sock, buffer, BUFFER_SIZE, 0);
    file.close();
}

int main() {
    int server_fd, client_sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    std::cout << "Server listening on port " << PORT << "...\n";

    client_sock = accept(server_fd, nullptr, nullptr);
    std::cout << "Client connected.\n";

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        recv(client_sock, buffer, BUFFER_SIZE, 0);

        std::string command(buffer);

        if (command == "LIST") {
            list_files(client_sock);
        }
        else if (command.rfind("UPLOAD", 0) == 0) {
            std::string filename = command.substr(7);
            receive_file(client_sock, filename);
        }
        else if (command.rfind("DOWNLOAD", 0) == 0) {
            std::string filename = command.substr(9);
            send_file(client_sock, filename);
        }
        else if (command == "EXIT") {
            break;
        }
    }

    close(client_sock);
    close(server_fd);
    return 0;
}
