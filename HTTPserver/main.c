#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif 
#include<winsock2.h>
#include<ws2tcpip.h>
#pragma comment (lib,"ws2_32.lib")

#else 
#include <sys/typed.h?>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#endif

#if defined(_WIN32)
#define ISVALIDSOCKET(s) ((s)!=INVALID_SOCKET)
#define CLOSESOCKET(s) closesocket(s)
#define GETSOCKETERRNO() (errno)
#endif

#define MAX_REQUEST_SIZE 2047;

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

SOCKET create_socket(const char* host, const char* port) {
	printf("Configuring local address...\n");
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo* bind_address;
	int result= getaddrinfo(host, port, &hints, &bind_address);
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
		exit(0);
	}
	freeaddrinfo(bind_address);

	printf("Listening ....\n");
	if (listen(socket_listen, 10) < 0) {
		fprintf(stderr, "listen() failed. %d\n", GETSOCKETERRNO());
		exit(1);
	}
	return socket_listen;
}

//we are now creating the structure for storing the client_info information of each client separately.
struct client_info {
	//it should contain the address, addrlen, request header, recieved bytes, next client_info, and the most important is the socket.
	socklen_t addrlen_length;
	struct sockaddr_storage address;
	SOCKET socket;
	char request[2048];
	int received;
	struct client_info* next;
};
static struct client_info* clients = 0;


//Get_client
struct client_info *get_client(SOCKET socket) {
	/*struct client_info client[10];
	for (int i = 0; i < 9; i++) {
		if (client[i].socket == socket) {
			
		}
	}*/
	struct client_info* c = clients;
	while (c) {
		if (c->socket == socket) {
			break;
		}
		c = c->next;
		if (c== NULL) {
			printf("No entry was found so creating the new client with the socket information ");
			struct client_info* n;
			n = (struct client_info *)calloc(1, sizeof(struct client_info));
			n->next = clients;
			clients = n;
			c = n;

		}
		return c;
	}
	
}

//now it is the time for deleting the clients of the given socket by using the drop_client()

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

//Get_client_address
const char* get_client_address(struct client_info *clients) {
	static char* address_buffer[100];
	getnameinfo((struct sockaddr*)&clients->address, clients->addrlen_length, address_buffer[100], 0, 0, NI_NUMERICHOST);
	return address_buffer;
}

//Now we are going to develop the code for handling the multiple clients and for this we will need to use the select function.
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
		fprints(stderr, "select() failed. %d\n", GETSOCKETERRNO());
		exit(1);
	}
	return reads;
}

//now lets deal with the error handing while sending the http request i.e. 404 and 400 error;
 //400=couldnot handle the request or the request is invalid 
 //404=requested file could not be found 

 void send_400(struct client_info *client) {
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

 char* get_content_type(const char* path) {
	//why the hell are you being so dependent use your memory sometimes also bro
	 //we have different content types right for example we have text/html , text/plain, application/json so which are typically the extensions of the files being requested 
	 // we can get the extensions of the file in the path variable right but we need to parse the variable path may be like /main/contact.html

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
	 return "application/octet-stream";
 
 }

 //serve resource 
 //we need to give access to the public folder not to the main parent folder and hence .. should not be allowed 
 //also it the path=/ then the public main folder should be served 
 //also we are going to place the path in the fulpath array using the sprintf function and also set the maximum lenght of the path as 128bytes 
 //now if the path contains the / character then what are we going to do I also dont know 
 
 void serve_resource(struct client_info* client, char* path) {
	 if (strcmp(path, '/') == 0) {
		 //here we need to serve the index file in the pulbic folder
		 path = "/index.html";
	 }
	 if (strstr(path, "..")) {
		 send_400(client);
		 return;
	 }
	 if (strlen(path) > 100) {
		 send_400(client);
		 return;
	 }
	 //Now we are going to use the sprintf function to store the path in the fulpath array
	 char* full_path[128];
	 sprintf(full_path, "public%s", path);
	 //now we have one more thing in the linux we can change the folder directory with the help of / but in windows we need the backslash \.
	 if (_WIN32) {
		 //If there are multiple /'s in the path variable then we need to change all to the backslash.
		 for (int i = 0; i < strlen(path); i++) {
			 if (*(path + i) == '/') {
				 *(path + i) = '\\';   //since the single backslash in C is the special character so we need to use double backslashed which will be counted as the single backslash;

			 }

		 }
	 }
	 //writing the code to open the requested file 
	 FILE* fp = fopen(full_path, 'rb');
	 if (!fp) {
		 return send_404(client);
		 return;
	 }
	 
	 //Now we need to get the size of the file as we need it to pass to the content size parameter of the header response 
	 //We are going to use the fseek() and ftell() functions to acheive that
	 //fseek() helps to move the file pointer to any speciefied place of the file which is specified as origin+buffer
	 //ftell() helps to get the current position of the cursor in the file 
	 fseek(fp, 0L, SEEK_END);
	//The above line will move the cursor to the end of the file as SEEK_END represent the end of the file and if 0 of long type is added to it, it will still point to the end of the cursor 
	 size_t c1 = ftell(fp);
	 //This will return the size of the file as the pointer is currenlty at the end of the file 
	 rewind(fp);
	 //This will bring the file pointer to the begining of the document 
	 //Also we need to get the type of the file requested for which we have already developed the get_content_type function
	 char content_type=get_content_type(full_path);

	 //Now its time to send the http response header to the client
	 //first we need to send the http response and then add a break line with /r/n code and then the http body i.e. the requested file is sent
	 //The order of sending the response is: http version, connection, file size, file type and finally the break line 
#define MAX_RESPONSE_SIZE 1024;
	 char buffer[1024];
	 sprintf(buffer, "HTTP/1.1 200 OK\r\n");
	 send(client->socket, buffer, sizeof(buffer), 0);

	 sprintf(buffer, "Connection: close\r\n");
	 send(client->socket, buffer, sizeof(buffer), 0);

	 sprintf(buffer, "Content-Length: %u\r\n", c1);
	 send(client->socket, buffer, sizeof(buffer), 0);

	 sprintf(buffer, "Content-Type: %s\r\n", content_type);
	 send(client->socket, buffer, sizeof(buffer), 0);

	 sprintf(buffer, "\r\n");
	 send(client->socket, buffer, sizeof(buffer), 0);
	 //This is the end of the http response now the http body will start from here which will serve the file to the client 
	 //Now I think we are going to read one character in one time from the file stream
	 int full_items=fread(buffer, 1, 1024, fp);
	 while (full_items) {
		 send(client->socket, buffer, sizeof(buffer), 0);
		 full_items=fread(buffer, 1, 1024, fp);
	 }

	 fclose(fp);
	 drop_client(client);
	 //now we have got data in the buffer so we need to send the data.



 }

int main() {
#if defined(_WIN32)
	WSADATA d;
	if (WSAStartup(MAKEWORD(2, 2), &d)) {
		fprintf(stderr, "Failed to initialize.\n");
		return 1;
	}
#endif
	SOCKET server=create_socket("127.0.0.1", "8090");

	while (1) {
		fd_set reads;
		reads = wait_on_clients(server);
		if (FD_ISSET(server, &reads)) {
			struct client_info* client = get_client(-1);  //The -1 is the invalid parameter so it will create the new client ;
			client->socket = accept(server,(struct sockaddr*)&(client->address),&(client->addrlen_length));
			if (!ISVALIDSOCKET(client->socket)) {
				fprintf(stderr, "accept() failed. (%d)\n",GETSOCKETERRNO());
				return 1;
			}
			printf("New connection from %s.\n",get_client_address(client));
		}
		struct client_info* client = clients;
		while (client) {
			struct client_info* next = client->next;
			if (FD_ISSET(client->socket, &reads)) {
				if (2048 == client->received) {
					send_400(client);
					continue;
				}
				int r = recv(client->socket,
					client->request + client->received,2048 - client->received, 0);
				if (r < 1) {
					printf("Unexpected disconnect from %s.\n",get_client_address(client));
					drop_client(client);
				}
				else {
					client->received += r;
					client->request[client->received] = 0;
					char* q = strstr(client->request, "\r\n\r\n");
					if (q) {
						if (strncmp("GET /", client->request, 5)) {
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
					} //if (q)
				}
			}
			client = next;
		}
	} //while(1)
	printf("\nClosing socket...\n");
	CLOSESOCKET(server);
#if defined(_WIN32)
	WSACleanup();
#endif
	printf("Finished.\n");
	return 0;
}

