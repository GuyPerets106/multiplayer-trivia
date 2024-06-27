// Compile with: gcc server.c -o server -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define PORT 8080
#define MULTICAST_IP "239.0.0.1"
#define MULTICAST_PORT 12345
#define MAX_CLIENTS 100

typedef struct {
    int socket;
    struct sockaddr_in address;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; 

void *new_client_handler(void *arg) {
    Client client = *(Client *)arg;
    char buffer[1024]; // ?

    // Receive client's readiness for the quiz
    recv(client.socket, buffer, sizeof(buffer), 0);
    printf("Client response: %s\n", buffer);

    // Simulate receiving answers
    while (1) {
        int len = recv(client.socket, buffer, sizeof(buffer), 0);
        if (len <= 0) break;
        buffer[len] = '\0';
        printf("Received answer from client: %s\n", buffer);
    }

    close(client.socket);
    return NULL;
}

void broadcast_question(const char *question) {
    int sock;
    struct sockaddr_in multicast_addr;
    struct ip_mreq multicast_request;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP); // UDP socket
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_IP);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    sendto(sock, question, strlen(question), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
    close(sock);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t thread_id;

    // Create TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
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

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started. Waiting for connections...\n");

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        pthread_mutex_lock(&client_mutex);
        clients[client_count].socket = new_socket;
        clients[client_count].address = address;
        client_count++;
        pthread_mutex_unlock(&client_mutex);

        pthread_create(&thread_id, NULL, new_client_handler, &clients[client_count - 1]);
        pthread_detach(thread_id);

        printf("New client connected: %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    }

    close(server_fd);
    return 0;
}