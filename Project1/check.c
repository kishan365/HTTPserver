#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif 
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib,"ws2_32.lib")
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
        fprintf(stderr, "bind failed(). %d\n", GETSOCKETERRNO());
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
    n->socket = socket;
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

const char* get_client_address(struct client_info* clients) {
    static char address_buffer[100];
    getnameinfo((struct sockaddr*)&clients->address, clients->addrlen_length, address_buffer, sizeof(address_buffer), NULL, 0, NI_NUMERICHOST);
    return address_buffer;
}

fd_set wait_on_clients(SOCKET socket) {
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(socket, &reads);
    struct client_info* n = clients;
    SOCKET max_socket = socket;
    while (n) {
        FD_SET(n->socket, &reads);
        if (n->socket > max_socket)
            max_socket = n->socket;
        n = n->next;
    }
    if (select(max_socket + 1, &reads, 0, 0, 0) < 0) {
        fprintf(stderr, "select() failed. %d\n", GETSOCKETERRNO());
        exit(1);
    }
    return reads;
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
    return "application/octet-stream"; // Default for unknown types
}

void serve_resource(struct client_info* client, char* path) {
    if (strcmp(path, "/") == 0) {
        //path = "/index.html";
        path = "C:\\Users\\kishan sah\\Desktop\\test.txt";
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
    sprintf_s(full_path, "public%s", path);

    FILE* fp = fopen(full_path, "rb");
    if (!fp) {
        send_404(client);
        return;
    }

    fseek(fp, 0L, SEEK_END);
    size_t file_size = ftell(fp);
    rewind(fp);

    char* content_type = get_content_type(full_path);

    char buffer[MAX_REQUEST_SIZE];
    sprintf_s(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf_s(buffer, sizeof(buffer), "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf_s(buffer, "Content-Length: %lu\r\n", (unsigned long)file_size);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf_s(buffer, "Content-Type: %s\r\n", content_type);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf_s(buffer, sizeof(buffer), "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, MAX_REQUEST_SIZE, fp)) > 0) {
        send(client->socket, buffer, bytes_read, 0);
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

    SOCKET server = create_socket("127.0.0.1", "8090");

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

        struct client_info* client = clients;
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
