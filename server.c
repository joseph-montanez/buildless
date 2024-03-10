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
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <stdbool.h>

#define MAX_CLIENTS 256
#define PORT 8080
#define BUFFER_SIZE 16384

time_t last_check_time = 0;
int client_sockets[MAX_CLIENTS];
bool client_needs_update[MAX_CLIENTS];
bool client_subscribed_to_watch[MAX_CLIENTS];
int num_clients = 0;

void set_socket_non_blocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return;
    }
    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
    }
}

void set_socket_blocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return;
    }
    flags &= ~O_NONBLOCK;
    if (fcntl(socket, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL ~O_NONBLOCK");
    }
}

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

int is_socket_alive(int socket_fd) {
    fd_set read_fds;
    struct timeval tv;
    int retval;
    char buffer[1];

    // Set up the file descriptor set.
    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);

    // Set up the struct timeval for the timeout.
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500 milliseconds timeout

    // Wait for readability or error.
    retval = select(socket_fd + 1, &read_fds, NULL, NULL, &tv);

    if (retval == -1) {
        // An error occurred with select()
        perror("select()");
        return 0;
    } else if (retval) {
        // Data is available, this means the socket is still alive.
        // Use MSG_PEEK to check the socket without removing data from the buffer
        retval = recv(socket_fd, buffer, 1, MSG_PEEK | MSG_DONTWAIT);
        if (retval > 0) {
            // Data is available to read, socket is alive.
            return 1;
        } else if (retval == 0 || (retval == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // The client has closed the connection or an error occurred
            return 0;
        }
    }
    // select() timed out, no data available, but the socket is not necessarily dead.
    // You might decide based on your application's logic whether to treat this as alive or not.
    // For the sake of this example, let's assume the socket is still alive.
    return 1;
}

void serve_watch(int socket) {
    char header[] = "HTTP/1.1 200 OK\n"
                    "Content-Type: text/event-stream\n"
                    "Cache-Control: no-cache\n"
                    "Connection: keep-alive\n\n";
    send(socket, header, strlen(header), MSG_NOSIGNAL | MSG_DONTWAIT); // Ensure non-blocking send

}

const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) {
        return "application/octet-stream"; // Default MIME type
    }
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".jsx") == 0) return "application/javascript";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
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
    struct timeval tv;
    fd_set read_fds, write_fds;

    // Initialize client_sockets array to -1
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_needs_update[i] = false;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_subscribed_to_watch[i] = false;
    }

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

    set_socket_non_blocking(server_fd);

    printf("Serving at port %d\n", PORT);

    while (1) {
        time_t current_mod_time = get_latest_mod_time();
        if (current_mod_time > last_check_time) {
            for (int i = 0; i < num_clients; i++) {
                if (client_sockets[i] != -1 && client_subscribed_to_watch[i]) {
                    client_needs_update[i] = true;
                    printf("Files changed, flagging client %d for update.\n", i);
                }
            }
            last_check_time = current_mod_time;
        }


        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(server_fd, &read_fds);

        int max_fd = server_fd;

        // Add client sockets to read_fds and write_fds
        for (int i = 0; i < num_clients; i++) {
            if (client_sockets[i] != -1) {
                FD_SET(client_sockets[i], &read_fds);
                FD_SET(client_sockets[i], &write_fds); // Only for clients that need updates
                if (client_sockets[i] > max_fd) {
                    max_fd = client_sockets[i];
                }
            }
        }

        // Set timeout for select()
        tv.tv_sec = 1; // Wake up every second to check for file changes
        tv.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, &tv);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error");
        }

        // Accept new connections
        if (FD_ISSET(server_fd, &read_fds)) {
            while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addr_len)) > 0) {
                set_socket_non_blocking(new_socket);      // Add new_socket to client_sockets array
                int added = 0; // Flag to check if socket was added
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == -1) { // Slot is available
                        client_sockets[i] = new_socket;
                        printf("Added new client on socket %d\n", new_socket);
                        added = 1;
                        break; // Exit the loop after adding socket
                    }
                }

                if (!added) {
                    printf("Reached max clients. Rejecting new connection.\n");
                    close(new_socket); // Close the socket if we can't add it to our list
                } else {
                    num_clients++;
                    break;
                }
            }
            if (new_socket < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept");
            }
        }

        // Handle client requests and /watch updates
        for (int i = 0; i < num_clients; i++) {
            if (client_sockets[i] == -1) continue;  // Skip invalid entries

            if (FD_ISSET(client_sockets[i], &read_fds)) {
                memset(buffer, 0, BUFFER_SIZE);
                ssize_t read_bytes = recv(client_sockets[i], buffer, BUFFER_SIZE - 1, 0);  // Changed from read() to recv()

                if (read_bytes > 0) {
                    // Parse request here (simplified)
                    char *method = strtok(buffer, " ");
                    char *path = strtok(NULL, " ");

                    if (method && path && strcmp(method, "GET") == 0) {
                        if (strcmp(path, "/watch") == 0) {
                            printf("Watch established.\n");
                            serve_watch(client_sockets[i]);  // Set up client for SSE
                            client_subscribed_to_watch[i] = true;  // Mark client as subscribed

                            // Don't automatically flag for update unless there's new content
                            if (get_latest_mod_time() > last_check_time) {
                                client_needs_update[i] = true;
                            }
                        } else {
                            char filepath[256] = ".";
                            strcat(filepath, path);  // Create filepath from the current directory
                            // Check if the file exists; if not, or if the request is for "/", serve index.html
                            struct stat filestat;
                            if (strcmp(path, "/") == 0 || stat(filepath, &filestat) < 0) {
                                strcpy(filepath, "./index.html");
                            }

                            // Note: Change `new_socket` to `client_sockets[i]` to reflect the correct socket in use.
                            serve_file(client_sockets[i], filepath);
                        }
                    } else {
                        // Note: Change `new_socket` to `client_sockets[i]` for consistency and correctness.
                        serve_file(client_sockets[i], "index.html");
                    }
                } else if (read_bytes == 0 || (read_bytes < 0 && (errno != EAGAIN && errno != EWOULDBLOCK))) {
                    // Client disconnected or read error occurred
                    close(client_sockets[i]);
                    client_sockets[i] = -1;  // Mark as available for a new connection
                    continue;
                }
            }
        }


        // After handling reads, handle writes (e.g., sending updates to /watch subscribers) separately
        // to ensure you're only writing when the socket is indeed ready for writing
        for (int i = 0; i < num_clients; i++) {
            if (client_sockets[i] == -1 || !client_needs_update[i]) continue; // Skip if not subscribed to /watch or no updates

            if (FD_ISSET(client_sockets[i], &write_fds)) {
                const char *update_message = "data: update\n\n";
                ssize_t sent_bytes = send(client_sockets[i], update_message, strlen(update_message), MSG_NOSIGNAL | MSG_DONTWAIT);

                if (sent_bytes < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        close(client_sockets[i]);
                        client_sockets[i] = -1; // Mark as available
                        client_needs_update[i] = false; // Reset flag
                    }
                } else {
                    client_needs_update[i] = false; // Consider resetting only after successful send
                }
            }
        }

    }

    return 0;
}