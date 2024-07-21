#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif 
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#define ISVALIDSOCKET(s) ((s) != INVALID_SOCKET)
#define CLOSESOCKET(s) closesocket(s)
#define GETSOCKETERRNO() (WSAGetLastError())
#else
#define SOCKET int
#define ISVALIDSOCKET(s) ((s) >= 0)
#define CLOSESOCKET(s) close(s)
#define GETSOCKETERRNO() (errno)
#endif

#define MAX_REQUEST_SIZE 2047

struct client_info {
    socklen_t addrlen_length;
    struct sockaddr_storage address;
    SOCKET socket;
    char request[2048];
    int received;
    struct client_info* next;
};
static struct client_info* clients = NULL;

SOCKET create_socket(const char* host, const char* port) {
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* bind_address;
    int result = getaddrinfo(host, port, &hints, &bind_address);
    if (result != 0) {
        fprintf(stderr, "getaddrinfo() failed. %d\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Creating the socket ....\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Binding socket to the local address...\n");
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. %d\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    printf("Listening ....\n");
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed. %d\n", GETSOCKETERRNO());
        exit(1);
    }
    return socket_listen;
}

struct client_info* get_client(SOCKET socket) {
    struct client_info* c = clients;
    while (c) {
        if (c->socket == socket) {
            return c;
        }
        c = c->next;
    }

    printf("Creating a new client for socket %d.\n", socket);
    struct client_info* n = (struct client_info*)calloc(1, sizeof(struct client_info));
    if (!n) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(1);
    }
    n->addrlen_length = sizeof(n->address);
    n->next = clients;
    clients = n;

    return n;
}

void drop_client(struct client_info* client) {
    CLOSESOCKET(client->socket);
    struct client_info** p = &clients;
    while (*p) {
        if (*p == client) {
            *p = client->next;
            free(client);
            return;
        }
        p = &(*p)->next;
    }
    fprintf(stderr, "drop_client not found.\n");
    exit(1);
}

const char* get_client_address(struct client_info* client) {
    static char address_buffer[100];
    getnameinfo((struct sockaddr*)&client->address, client->addrlen_length, address_buffer, sizeof(address_buffer), NULL, 0, NI_NUMERICHOST);
    return address_buffer;
}

fd_set wait_on_clients(SOCKET socket) {
    fd_set read;
    FD_ZERO(&read);
    FD_SET(socket, &read);

    struct client_info* n = clients;
    SOCKET max_socket = socket;
    while (n) {
        FD_SET(n->socket, &read);
        if (n->socket > max_socket)
            max_socket = n->socket;
        n = n->next;
    }

    struct timeval timeout;
    timeout.tv_sec = 7;
    timeout.tv_usec = 0;

    int result = select(max_socket + 1, &read, 0, 0, 0);
    if (result < 0) {
        fprintf(stderr, "select() failed. %d\n", GETSOCKETERRNO());
        exit(1);
    }
    if (FD_ISSET(socket, &read)) {
        printf("\nThe socket i.e. %d is present in the fdset", socket);
    }
    return read;
}

void send_400(struct client_info* client) {
    const char* c400 = "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Length: 11\r\n\r\nBad Request";
    send(client->socket, c400, strlen(c400), 0);
    drop_client(client);
}

void send_404(struct client_info* client) {
    const char* c404 = "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Length: 11\r\n\r\nNot Found";
    send(client->socket, c404, strlen(c404), 0);
    drop_client(client);
}

const char* get_content_type(const char* path) {
    // Implement content type determination based on file extension
    char* extracted_text = strrchr(path, '.');
    if (extracted_text) {
        extracted_text = extracted_text + 1;
        if (strcmp(extracted_text, "css") == 0) return "text/css";
        if (strcmp(extracted_text, "csv") == 0) return "text/csv";
        if (strcmp(extracted_text, "gif") == 0) return "image/gif";
        if (strcmp(extracted_text, "htm") == 0) return "text/html";
        if (strcmp(extracted_text, "html") == 0) return "text/html";
        if (strcmp(extracted_text, "ico") == 0) return "image/x-icon";
        if (strcmp(extracted_text, "jpeg") == 0) return "image/jpeg";
        if (strcmp(extracted_text, "jpg") == 0) return "image/jpeg";
        if (strcmp(extracted_text, "js") == 0) return "application/javascript";
        if (strcmp(extracted_text, "json") == 0) return "application/json";
        if (strcmp(extracted_text, "png") == 0) return "image/png";
        if (strcmp(extracted_text, "pdf") == 0) return "application/pdf";
        if (strcmp(extracted_text, "svg") == 0) return "image/svg+xml";
        if (strcmp(extracted_text, "txt") == 0) return "text/plain";
    }
    if (strcmp(path,"/")==0) {
        return "text/html";
    }
    return "application/octet-stream"; // Default for unknown types
}

void serve_resource(struct client_info* client, char* path) {
    if (strcmp(path, "/") == 0) {
        path = "C:\\Users\\kishan sah\\Desktop\\test.html";
    }
    if (strstr(path, "..")) {
        send_400(client);
        return;
    }
    if (strlen(path) > 100) {
        send_400(client);
        return;
    }

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s", path);

    FILE* fp = fopen(full_path, "rb");
    if (!fp) {
        send_404(client);
        return;
    }

    fseek(fp, 0L, SEEK_END);
    size_t file_size = ftell(fp);
    rewind(fp);

    const char* content_type = get_content_type(full_path);

    char buffer[MAX_REQUEST_SIZE];
    snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "Content-Length: %lu\r\n", (unsigned long)file_size);
    send(client->socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "Content-Type: %s\r\n", content_type);
    send(client->socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, MAX_REQUEST_SIZE, fp)) > 0) {
        if (send(client->socket, buffer, bytes_read, 0) < 0) {
            fprintf(stderr, "send() function was failed. %d", GETSOCKETERRNO());
        }
        
    }

    fclose(fp);
    drop_client(client);
}

int main() {
#if defined(_WIN32)
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }
#endif

    SOCKET server = create_socket("127.0.0.1", "8080");

    while (1) {
        fd_set reads;
        reads = wait_on_clients(server);
        if (FD_ISSET(server, &reads)) {
            struct client_info* client = get_client(-1);
            client->addrlen_length = sizeof(client->address);
            client->socket = accept(server, (struct sockaddr*)&(client->address), &(client->addrlen_length));
            if (!ISVALIDSOCKET(client->socket)) {
                fprintf(stderr, "accept() failed. (%d)\n", GETSOCKETERRNO());
                return 1;
            }
            printf("New connection from %s.\n", get_client_address(client));
        }
        //Now that we have the new connection established from the client, we can proceed to serve the file
        //First we need to parse the url and extract the path from the url. So for this we are going to create the function and get the path from the url

        //get_content_type("/");
        struct client_info* client = clients;
        //serve_resource(client, "/");   //Upto this point one client has been created and already placed into the 
        //client info structure.
        while (client) {
            struct client_info* next = client->next;

            if (FD_ISSET(client->socket, &reads)) {
                if (2048 == client->received) {
                    send_400(client);
                    continue;
                }
                int r = recv(client->socket, client->request + client->received, 2048 - client->received, 0);
                if (r < 1) {
                    printf("Unexpected disconnect from %s.\n", get_client_address(client));
                    drop_client(client);
                }
                else {
                    client->received += r;
                    client->request[client->received] = 0;
                    char* q = strstr(client->request, "\r\n\r\n");
                    if (q) {
                        if (strncmp("GET /", client->request, 5) != 0) {
                            send_400(client);
                        }
                        else {
                            char* path = client->request + 4;
                            char* end_path = strstr(path, " ");
                            if (!end_path) {
                                send_400(client);
                            }
                            else {
                                *end_path = 0;
                                serve_resource(client, path);
                            }
                        }
                    } // if (q)
                }
            }
            client = next;
        }
    } // while(1)

    printf("\nClosing socket...\n");
    CLOSESOCKET(server);

#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}
