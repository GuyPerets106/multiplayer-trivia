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
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define PORT 8080
#define MULTICAST_IP "228.6.73.122"
#define MULTICAST_PORT 12345
#define MAX_CLIENTS 100
#define START_GAME_TIMEOUT 30

typedef struct {
    int socket;
    struct sockaddr_in address;
    int authenticated;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; 
char* auth_code;

char* generate_random_code() { // Generates a 6 characters code (a-z/A-Z/0-9)
    srand(time(NULL));
    char* code = (char*)malloc(7);
    for (int i = 0; i < 6; i++)
    {
        int type = rand() % 3;
        switch (type)
        {
        case 0:
            code[i] = 'a' + rand() % 26;
            break;
        case 1:
            code[i] = 'A' + rand() % 26;
            break;
        case 2:
            code[i] = '0' + rand() % 10;
            break;
        }
    }
    code[6] = '\0';
    return code;
}


void* authenticate_client(void* arg) {
    int* fds = (int*)arg;
    int server_fd = fds[0];
    int socket = fds[1];

    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags & ~O_NONBLOCK);
    char auth_buffer[1024];
    while(1){
        memset(auth_buffer, 0, sizeof(auth_buffer));
        int ret = recv(socket, auth_buffer, sizeof(auth_buffer), 0);
        if (ret == 0) break; // Closed socket
        else if (ret < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                usleep(100000);  // Sleep for 100 milliseconds
                continue;
            }
            else {
                perror("Error receiving authentication code");
                break;
            }
        }
        printf("Received authentication code: %s\n", auth_buffer);
        if (strcmp(auth_buffer, auth_code) != 0) {
            send(socket, "Invalid authentication code", 27, 0);
            continue;
        }
        else {
            send(socket, "Authentication successful", 26, 0);
            break;
        }
    }
    free(arg);
    return NULL;
}

void *new_client_handler(void *arg) {
    Client client = *(Client *)arg;
    char buffer[1024];

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
    auth_code = generate_random_code();

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

    printf("Server started with authentication code %s. Waiting for connections...\n", auth_code);
    time_t start_time = time(NULL);
    while (1) {
        // Get the current time
        time_t current_time = time(NULL);

        // Calculate the elapsed time
        double elapsed_time = difftime(current_time, start_time);

        // Check if the elapsed time is greater than the allowed time frame
        if (elapsed_time > START_GAME_TIMEOUT) {
            printf("Time frame of %d seconds has expired. No more connections accepted.\n", START_GAME_TIMEOUT);
            break;
        }

        // Set the socket to non-blocking mode
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No connection yet, sleep for a short time and try again
                usleep(100000);  // Sleep for 100 milliseconds
                continue;
            } else {
                perror("accept");
                break;
            }
        }
        printf("New request received\n");
        int* fds = malloc(2 * sizeof(int));
        if (fds == NULL) {
            perror("malloc");
            close(new_socket);
            continue;
        }

        // Initialize the arguments
        fds[0] = server_fd;
        fds[1] = new_socket;

        pthread_mutex_lock(&client_mutex);
        // Change client to authenticated = 0
        clients[client_count].authenticated = 0;
        pthread_mutex_unlock(&client_mutex);
        
        // Create a new thread to handle the connection
        if (pthread_create(&thread_id, NULL, authenticate_client, (void*)fds) != 0) {
            perror("pthread_create");
            close(new_socket);
            free(fds);
            continue;
        }
        pthread_detach(thread_id);
        
        pthread_mutex_lock(&client_mutex);
        if (clients[client_count].authenticated == 0) {
            close(new_socket);
            pthread_mutex_unlock(&client_mutex);
            continue;
        }
        else {
        clients[client_count].socket = new_socket;
        clients[client_count].address = address;
        client_count++;
        pthread_mutex_unlock(&client_mutex);
        printf("New connection accepted: %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        }

        pthread_create(&thread_id, NULL, new_client_handler, &clients[client_count - 1]);
        pthread_detach(thread_id);

        printf("New client connected: %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    }
    printf("Game started. Broadcasting questions...\n");
    while(1);
    close(server_fd);
    return 0;
}