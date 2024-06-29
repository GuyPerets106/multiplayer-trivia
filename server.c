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

#define AUTH_SUCCESS_MSG "Authentication successful"
#define AUTH_FAIL_MSG "Invalid authentication code"
#define MAX_TRIES_MSG "Maximum number of tries exceeded"
#define GAME_STARTED_MSG "Game already started"
#define KEEP_ALIVE_MSG "Keep alive"

#define AUTH_SUCCESS 0
#define AUTH_FAIL 1
#define MAX_TRIES 2
#define GAME_STARTED 3
#define GAME_STARTING 4
#define QUESTION 5
#define KEEP_ALIVE 6
#define SCOREBOARD 7

typedef struct {
    int socket;
    struct sockaddr_in address;
} Client;

typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    int addrlen;
} SocketInfo;

// Define the message structure
typedef struct {
    int type;  // Message type
    char data[1024];  // Message data
} Message;

void send_message(int sock, int msg_type, const char *msg_data) {
    Message msg;
    msg.type = msg_type;
    strncpy(msg.data, msg_data, sizeof(msg.data) - 1);
    msg.data[sizeof(msg.data) - 1] = '\0';  // Ensure null-termination

    // Send the message
    send(sock, &msg, sizeof(msg), 0);
}

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

void* broadcast_keep_alive(void* arg) {
    int sock;
    struct sockaddr_in multicast_addr;
    struct ip_mreq multicast_request;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP); // UDP socket
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_IP);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    while (1) {
        sendto(sock, KEEP_ALIVE_MSG, strlen(KEEP_ALIVE_MSG), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
        sleep(1);
    }
    close(sock);
    return NULL;
}


void* authenticate_client(void* arg) {
    Client* client = (Client*)arg;
    int socket = client->socket;
    struct sockaddr_in address = client->address;
    int wrong_auth_counter = 0;

    // Set the socket to blocking mode
    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags & ~O_NONBLOCK);

    char auth_buffer[1024];
    while(1){
        memset(auth_buffer, 0, sizeof(auth_buffer));
        int ret = recv(socket, auth_buffer, sizeof(auth_buffer), 0);
        if (ret == 0) { // Closed socket
            free(arg);
            return NULL;
        }
        else if (ret < 0) {
            perror("Error receiving authentication code");
            free(arg);
            return NULL;
        }

        printf("Received authentication code: %s\n", auth_buffer);
        if (strcmp(auth_buffer, auth_code) != 0) {
            send_message(socket, AUTH_FAIL, AUTH_FAIL_MSG);
            wrong_auth_counter++;
            if (wrong_auth_counter < 5) {
                continue;
            }
            else {
                send_message(socket, MAX_TRIES, MAX_TRIES_MSG);
                close(socket);
                free(arg);
                return NULL;
            }
        }
        else {
            send_message(socket, AUTH_SUCCESS, AUTH_SUCCESS_MSG);
            pthread_mutex_lock(&client_mutex);
            clients[client_count].socket = socket;
            clients[client_count].address = address;
            client_count++;
            pthread_mutex_unlock(&client_mutex);
            break;
        }
    }
    printf("New connection accepted: %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
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

void* wait_for_connections(void* arg){
    SocketInfo* info = (SocketInfo*)arg;
    int server_fd = info->socket_fd;
    struct sockaddr_in address = info->address;
    int addrlen = sizeof(address);
    time_t start_time = time(NULL);
    int new_socket;
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
                perror("Error in accept function");
                break;
            }
        }
        printf("New request received\n");
        Client* client_info = (Client*)malloc(sizeof(Client));
        client_info->socket = new_socket;
        client_info->address = address;

        // Create a new thread to handle the connection
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, authenticate_client, (void*)client_info) != 0) {
            perror("pthread_create");
            close(new_socket);
            free(client_info);
            continue;
        }
        pthread_detach(thread_id);
    }
    free(arg);
    return NULL;
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

    // Start Connection Phase - Wait For Connections Thread
    pthread_t wait_for_connections_thread;
    SocketInfo* info = (SocketInfo*)malloc(sizeof(SocketInfo));
    info->socket_fd = server_fd;
    info->address = address;
    info->addrlen = addrlen;
    pthread_create(&wait_for_connections_thread, NULL, wait_for_connections, (void*)info);
    pthread_join(wait_for_connections_thread, NULL); // Wait until connection phase is done

    // Start Game Phase
    printf("Game Starting...\n");
    pthread_t keep_alive_thread;
    pthread_create(&keep_alive_thread, NULL, broadcast_keep_alive, NULL); // Multicast keep alive messages
    pthread_detach(keep_alive_thread);

    while(1);
    close(server_fd);
    return 0;
}