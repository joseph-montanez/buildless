#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <fnmatch.h>
#include <signal.h>


#define PORT 8080
#define BUFFER_SIZE 4096

time_t last_check_time = 0;

void check_file_mod_time(const char *path, time_t *latest_mod_time) {
    struct stat statbuf;
    if (stat(path, &statbuf) == 0) {
        if (statbuf.st_mtime > *latest_mod_time) {
            *latest_mod_time = statbuf.st_mtime;
        }
    }
}

void find_files_and_update_time(const char *dir, time_t *latest_mod_time) {
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    char fullPath[1024];

    if ((dp = opendir(dir)) == NULL) {
        fprintf(stderr, "Cannot open directory: %s\n", dir);
        return;
    }

    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, entry->d_name);
        if (stat(fullPath, &statbuf) == -1) continue;

        if (S_ISDIR(statbuf.st_mode)) {
            // Recursively find in subdirectories
            find_files_and_update_time(fullPath, latest_mod_time);
        } else {
            // Update the modification time if the file matches the patterns
            if (fnmatch("*.html", entry->d_name, 0) == 0 ||
                fnmatch("*.css", entry->d_name, 0) == 0 ||
                fnmatch("*.js", entry->d_name, 0) == 0) {
                check_file_mod_time(fullPath, latest_mod_time);
            }
        }
    }
    closedir(dp);
}

time_t get_latest_mod_time() {
    time_t latest_mod_time = 0;
    find_files_and_update_time(".", &latest_mod_time); // Start from current directory
    return latest_mod_time;
}

void serve_watch(int socket) {
    const time_t current_mod_time = get_latest_mod_time();
    char response[100]; // Increased size to accommodate additional headers

    if (current_mod_time > last_check_time) {
        // Body is "1", so Content-Length is 1
        sprintf(response, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 1\n\n1");
        last_check_time = current_mod_time; // Update the last check time
    } else {
        // Body is "0", so Content-Length is also 1
        sprintf(response, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 1\n\n0");
    }

    const ssize_t bytes_sent = send(socket, response, strlen(response), 0);
    if (bytes_sent == -1) {
        perror("send failed");
        // Handle the error, maybe close the socket
        close(socket);
    }
}

const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) {
        return "application/octet-stream"; // Default MIME type
    }
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    // Add more MIME types here as needed.
    return "application/octet-stream"; // Default MIME type for unknown extensions
}

long get_file_size(FILE *file) {
    fseek(file, 0, SEEK_END); // Seek to the end of the file
    long size = ftell(file);  // Get the current file pointer
    rewind(file);             // Rewind file pointer to the start of the file
    return size;
}

void serve_file(int socket, const char* path) {
    char buffer[BUFFER_SIZE];
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        // File not found, serve index.html
        file = fopen("index.html", "r");
        path = "index.html"; // Reset path to index.html for correct MIME type
        if (file == NULL) {
            perror("Error opening file");
            exit(EXIT_FAILURE);
        }
    }

    long file_size = get_file_size(file);
    const char* mime_type = get_mime_type(path);
    char header[BUFFER_SIZE];
    sprintf(header, "HTTP/1.1 200 OK\nContent-Type: %s\nContent-Length: %ld\n\n", mime_type, file_size);
    send(socket, header, strlen(header), 0);

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        ssize_t result = send(socket, buffer, bytes_read, MSG_NOSIGNAL);
        if (result == -1) {
            // Handle the error. For example, break out of the loop and close the socket
            perror("send");
            break;
        }
    }

    fclose(file);
}

int main() {

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;  // This is used to set SO_REUSEADDR
    int addr_len = sizeof(address);
    char buffer[BUFFER_SIZE];

    // Ignore SIGPIPE signals
    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set SO_REUSEADDR to address the "Address already in use" error message
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
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

        // while (path && (*path == '.' || *path == '/')) {
        //     path++;  // Adjust the pointer to skip these characters.
        // }
        printf("Requested path: %s\n", path); // Print the path

        if (path && strcmp(method, "GET") == 0) {
            // Special endpoint handling
            if (strcmp(path, "/watch") == 0) {
                serve_watch(new_socket);
            } else {
                char filepath[256] = ".";
                strcat(filepath, path); // Create filepath from the current directory
                // Check if the file exists; if not, or if the request is for "/", serve index.html
                struct stat filestat;
                if (strcmp(path, "/") == 0 || stat(filepath, &filestat) < 0) {
                    strcpy(filepath, "./index.html");
                }
                serve_file(new_socket, filepath);
            }
        } else {
            // For simplicity, serve index.html if the request is not a GET or the path is not parsed
            serve_file(new_socket, "index.html");
        }

        close(new_socket);
    }

    return 0;
}