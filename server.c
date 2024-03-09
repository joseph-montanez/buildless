#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>

#define PORT 8080
#define BUFFER_SIZE 4096

void serve_file(int socket, const char* path) {
    char header[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";
    char buffer[BUFFER_SIZE];
    FILE *file = fopen(path, "r");

    if (file == NULL) {
        // File not found, serve index.html
        file = fopen("index.html", "r");
        if (file == NULL) {
            perror("Error opening file");
            exit(EXIT_FAILURE);
        }
    }

    send(socket, header, sizeof(header) - 1, 0);

    while (fgets(buffer, BUFFER_SIZE, file) != NULL) {
        send(socket, buffer, strlen(buffer), 0);
    }

    fclose(file);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addr_len = sizeof(address);
    char buffer[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Serving at port %d\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addr_len);
        if (new_socket < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        memset(buffer, 0, BUFFER_SIZE);
        read(new_socket, buffer, BUFFER_SIZE - 1);

        // Simple parsing to extract the requested URL path
        char *method = strtok(buffer, " ");
        char *path = strtok(NULL, " ");
        if (path && strcmp(method, "GET") == 0) {
            char filepath[256] = ".";
            strcat(filepath, path); // Create filepath from the current directory
            // Check if the file exists; if not, or if the request is for "/", serve index.html
            struct stat filestat;
            if (strcmp(path, "/") == 0 || stat(filepath, &filestat) < 0) {
                strcpy(filepath, "./index.html");
            }
            serve_file(new_socket, filepath);
        } else {
            // For simplicity, serve index.html if the request is not a GET or the path is not parsed
            serve_file(new_socket, "index.html");
        }

        close(new_socket);
    }

    return 0;
}