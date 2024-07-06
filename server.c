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
#define KEEP_ALIVE_INTERVAL 5
#define QUESTION_TIMEOUT 10
#define KEEP_ALIVE_TIMEOUT 15
#define SCOREBOARD_BREAK 5

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
#define ANSWER 6
#define KEEP_ALIVE 7
#define SCOREBOARD 8

typedef struct {
    int socket;
    struct sockaddr_in address;
    time_t last_keep_alive_time;
    int score;
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

typedef struct {
    int socket;
    Message msg;
} ClientMsg;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; 
char* auth_code;
char curr_question[1024];

void print_participants() {
    printf("Participants:\n");
    for (int i = 0; i < client_count; i++) {
        printf("%s:%d\n", inet_ntoa(clients[i].address.sin_addr), ntohs(clients[i].address.sin_port));
    }
}

void send_message(int sock, int msg_type, const char *msg_data) {
    Message msg;
    msg.type = msg_type;
    strncpy(msg.data, msg_data, sizeof(msg.data) - 1);
    msg.data[sizeof(msg.data) - 1] = '\0';  // Ensure null-termination

    // Send the message
    send(sock, &msg, sizeof(msg), 0);
}

void send_multicast_message(int sock, struct sockaddr_in addr, int msg_type, const char *msg_data) {
    Message msg;
    msg.type = msg_type;
    strncpy(msg.data, msg_data, sizeof(msg.data) - 1);
    msg.data[sizeof(msg.data) - 1] = '\0';  // Ensure null-termination

    // Send the message
    sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&addr, sizeof(addr));
}

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

void handle_keep_alive(int client_sock) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == client_sock) {
            clients[i].last_keep_alive_time = time(NULL);
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void* send_keep_alive(void* arg) { // Multicast
    SocketInfo* multicast_info = (SocketInfo*)arg;
    int multicast_sock = multicast_info->socket_fd;
    struct sockaddr_in multicast_addr = multicast_info->address;

    while (1) {
        send_multicast_message(multicast_sock, multicast_addr, KEEP_ALIVE, KEEP_ALIVE_MSG);
        printf("Sent keep alive message\n");
        sleep(KEEP_ALIVE_INTERVAL);
        // Check if specific clients did not send keep alive messages
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < client_count; i++) {
            time_t current_time = time(NULL);
            if (current_time - clients[i].last_keep_alive_time > KEEP_ALIVE_TIMEOUT) {
                printf("Client %s:%d did not send keep alive messages. Removing him from the game...\n", inet_ntoa(clients[i].address.sin_addr), ntohs(clients[i].address.sin_port));
                close(clients[i].socket); // ! Alon thinks we should try again with unicast message
                for (int j = i; j < client_count - 1; j++) {
                    clients[j] = clients[j + 1];
                }
                client_count--;
            }
        }
        pthread_mutex_unlock(&client_mutex);
    }
}


void* authenticate_client(void* arg) {
    Client* client = (Client*)arg;
    int socket = client->socket;
    struct sockaddr_in address = client->address;
    int wrong_auth_counter = 0;

    char auth_buffer[1024];
    while(1){
        memset(auth_buffer, 0, sizeof(auth_buffer));
        int ret = recv(socket, auth_buffer, sizeof(auth_buffer), 0);
        if (ret == 0) { // Closed socket
            return NULL;
        }
        else if (ret < 0) {
            perror("Error receiving authentication code");
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
                printf("Maximum number of tries exceeded. Closing connection.\n");
                // usleep(1000000); 
                close(socket);
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
    return NULL;
}

void* distribute_multicast_address(void* arg){ // Using unicast messages
    for (int i = 0; i < client_count; i++) {
        char multicast_address[1024];
        sprintf(multicast_address, "%s:%d", MULTICAST_IP, MULTICAST_PORT); // Creating the multicast address
        send_message(clients[i].socket, GAME_STARTING, multicast_address);
    }
    sleep(1);
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

void* wait_for_connections(void* arg){
    SocketInfo* info = (SocketInfo*)arg;
    int server_fd = info->socket_fd; // Welcome Socket
    struct sockaddr_in address = info->address;
    int addrlen = sizeof(address);
    fd_set read_fds;   // set of socket descriptors we want to read from
    time_t start_time = time(NULL);
    int client_socket;
    int max_fd = server_fd;


    while(1) {
        fd_set tmp_fds = read_fds; // Create a copy of the read_fds
        // Get the current time
        time_t current_time = time(NULL);

        // Calculate the elapsed time
        int elapsed_time = (int)difftime(current_time, start_time);
        struct timeval tv;
        tv.tv_sec = START_GAME_TIMEOUT - elapsed_time;
        tv.tv_usec = 0;
        FD_ZERO(&tmp_fds);
        FD_SET(server_fd, &tmp_fds);
        int ret = select(max_fd + 1, &tmp_fds, NULL, NULL, &tv); // Blocking until a connection is received or timeout
        if (ret < 0 && errno != EINTR) {
            perror("Error in select function");
            break; // ? WHAT ELSE SHOULD HAPPEN
            // exit(EXIT_FAILURE);
        }
        if (ret == 0) { // Timeout
            printf("Time frame of %d seconds has expired. No more connections accepted.\n", START_GAME_TIMEOUT);
            sleep(2);
            break;
        }
        if (FD_ISSET(server_fd, &tmp_fds)) {
            client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen); // ! BLOCKING
            if (client_socket < 0) {
                perror("Error in accept function");
                exit(EXIT_FAILURE);
            }
            printf("New request received, wait for authentication...\n");
            FD_SET(client_socket, &read_fds); // Add the new client to the read set
            max_fd = client_socket;
            Client client_info;
            client_info.socket = client_socket;
            client_info.address = address; // This is the address of the WelcomeSocket
            client_info.last_keep_alive_time = time(NULL);
            client_info.score = 0;

            // Create a new thread to handle the connection
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, authenticate_client, (void*)&client_info) != 0) {
                perror("pthread_create");
                close(client_socket);
                continue;
            }
            pthread_detach(thread_id);
            FD_CLR(server_fd, &read_fds);
        }
    }
    return NULL;
}

void* deny_new_connections(void* arg) {
    SocketInfo* info = (SocketInfo*)arg;
    int server_fd = info->socket_fd; // Welcome Socket
    struct sockaddr_in address = info->address;
    int addrlen = info->addrlen;

    while(1) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_socket < 0) {
            perror("Error in accept function");
            exit(EXIT_FAILURE);
        }
        send_message(client_socket, GAME_STARTED, GAME_STARTED_MSG);
        close(client_socket);
    }
}

void read_question_from_file() {
    // Read 5 lines each time a question is needed
    // Empty curr_question
    curr_question[0] = '\0';
    FILE* file = fopen("QUESTIONS.txt", "r");
    if (file == NULL) {
        perror("Error opening file");
    }
    for (int i = 0; i < 5; i++) {
        char line[1024];
        if (fgets(line, sizeof(line), file) == NULL) {
            perror("Error reading file");
        }
        strcat(curr_question, line);
    }
    fclose(file);
}


void send_scoreboard(int multicast_sock, struct sockaddr_in multicast_addr) {
    char scoreboard[1024];
    sprintf(scoreboard, "\n==========Scoreboard==========\n");
    for (int i = 0; i < client_count; i++) {
        char client_score[1024];
        sprintf(client_score, "%s:%d\t%d\n", inet_ntoa(clients[i].address.sin_addr), ntohs(clients[i].address.sin_port), clients[i].score);
        strcat(scoreboard, client_score);
    }
    strcat(scoreboard, "\n");
    send_multicast_message(multicast_sock, multicast_addr, SCOREBOARD, scoreboard);
    printf("%s", scoreboard);
}

void* send_questions(void* args){
    int num_questions = 10;
    while(1) {
        if (client_count == 0){
            break;
        }
        SocketInfo* info = (SocketInfo*)args;
        int multicast_sock = info->socket_fd;
        struct sockaddr_in multicast_addr = info->address;
        read_question_from_file(); 
        // printf("Question: %s\n", curr_question);
        // Send the questions through multicast
        send_multicast_message(multicast_sock, multicast_addr, QUESTION, curr_question);
        sleep(QUESTION_TIMEOUT);
        send_scoreboard(multicast_sock, multicast_addr);
        sleep(SCOREBOARD_BREAK);
        num_questions--;
        if (num_questions == 0){
            break;
        }
    }
    return NULL;
}

void handle_client_answer(int client_sock, char* answer) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == client_sock) {
            if (strcmp(answer, "b") == 0) { // ! CHANGE
                clients[i].score++; // ! CHANGE
            }
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void* handle_client_msg(void* arg){
    ClientMsg* client_msg = (ClientMsg*)arg;
    Message* msg = &client_msg->msg;
    int sock = client_msg->socket;
    switch (msg->type)
    {
        case ANSWER:
            printf("Received answer from client: %s\n", msg->data);
            handle_client_answer(sock, msg->data);
            break;
        case KEEP_ALIVE:
            printf("Received keep alive message: %s\n", msg->data);
            handle_keep_alive(sock);
            break;
        default:
            break;
    }
    return NULL;
}

void* listen_for_messages(void* args){
    // Every Unicast message coming for a specific client 
    // will be handled in another thread
    int sock = *(int*)args;
    Message msg;
    ClientMsg client_msg;
    int bytes_receive_unicast =  0;
    while(1){
        bytes_receive_unicast = recv(sock, &msg, sizeof(msg), 0);
        if (bytes_receive_unicast == 0) { // Closed socket
            printf("Client disconnected\n");
            close(sock);
            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < client_count; i++) {
                if (clients[i].socket == sock) {
                    for (int j = i; j < client_count - 1; j++) {
                        clients[j] = clients[j + 1];
                    }
                    client_count--;
                    break;
                }
            }
            pthread_mutex_unlock(&client_mutex);
            return NULL;
        }
        else if (bytes_receive_unicast < 0) {
            perror("Error receiving message");
            close(sock);
            return NULL;
        }
        client_msg.msg = msg;
        client_msg.socket = sock;
        pthread_t handle_message_thread;
        pthread_create(&handle_message_thread, NULL, handle_client_msg, (void*)&client_msg);
        pthread_detach(handle_message_thread);
    }
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
    SocketInfo info;
    info.socket_fd = server_fd;
    info.address = address;
    info.addrlen = addrlen;
    pthread_create(&wait_for_connections_thread, NULL, wait_for_connections, (void*)&info);
    pthread_join(wait_for_connections_thread, NULL); // Wait until connection phase is done


    // Start Game Phase:
    // 1. Distribute Multicast Address
    // 2. Start Keep Alive Thread
    // 3. Start Game (Multicast Questions, Scoreboard, etc.)
    int multicast_sock;
    struct sockaddr_in multicast_addr;
    struct ip_mreq multicast_request;

    multicast_sock = socket(AF_INET, SOCK_DGRAM, 0); // UDP socket
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_IP);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    printf("Game Starting...\n");
    pthread_t game_starting_thread;
    pthread_create(&game_starting_thread, NULL, distribute_multicast_address, NULL);
    pthread_join(game_starting_thread, NULL);

    pthread_t deny_connections_thread;
    pthread_create(&deny_connections_thread, NULL, deny_new_connections, (void*)&info); // Deny new connections
    pthread_detach(deny_connections_thread);

    SocketInfo multicast_info;
    multicast_info.socket_fd = multicast_sock;
    multicast_info.address = multicast_addr;
    multicast_info.addrlen = sizeof(multicast_addr);

    for(int i = 0; i < client_count; i++){
        clients[i].last_keep_alive_time = time(NULL);
    }
    pthread_t keep_alive_thread;
    pthread_create(&keep_alive_thread, NULL, send_keep_alive, (void*)&multicast_info); // Multicast keep alive messages
    pthread_detach(keep_alive_thread);

    // Open listen_for_messages thread for each client (Unicast)
    for(int i = 0; i < client_count; i++){
        pthread_t listen_for_messages_thread;
        pthread_create(&listen_for_messages_thread, NULL, listen_for_messages, (void*)&clients[i].socket);
        pthread_detach(listen_for_messages_thread);
    }

    pthread_t send_questions_thread;
    pthread_create(&send_questions_thread, NULL, send_questions, (void*)&multicast_info); // Multicast questions
    pthread_join(send_questions_thread, NULL);

    while(1); 
    // Kill the thread
    pthread_cancel(keep_alive_thread);
    pthread_cancel(deny_connections_thread);
    close(server_fd);
    return 0;
}