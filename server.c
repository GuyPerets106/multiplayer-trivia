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
#include <math.h>
#include <ifaddrs.h>
#include <net/if.h>


#define PORT 8080
#define INTERFACE_NAME "eno1"
#define MULTICAST_IP "228.6.73.122"
#define MULTICAST_PORT 12345
#define MAX_CLIENTS 100
#define START_GAME_TIMEOUT 30
#define KEEP_ALIVE_INTERVAL 5
#define QUESTION_TIMEOUT 10
#define KEEP_ALIVE_TIMEOUT 15
#define SCOREBOARD_BREAK 5
#define NUM_OF_QUESTIONS 45
#define QUESTION_TIMEOUT_BREAK 1

#define AUTH_SUCCESS_MSG "Authentication successful"
#define AUTH_FAIL_MSG "Invalid authentication code"
#define MAX_TRIES_MSG "Maximum number of tries exceeded"
#define GAME_STARTED_MSG "Game already started"
#define KEEP_ALIVE_MSG "Keep alive"
#define FILENAME "QUESTIONS.txt"

#define AUTH_SUCCESS 0
#define AUTH_FAIL 1
#define MAX_TRIES 2
#define GAME_STARTED 3
#define GAME_STARTING 4
#define QUESTION 5
#define ANSWER 6
#define KEEP_ALIVE 7
#define SCOREBOARD 8
#define GAME_OVER 9
#define INVALID 10

void handle_client_answer(int client_sock, char* client_answer);

typedef struct {
    int socket;
    struct sockaddr_in address;
    time_t last_keep_alive_time;
    int score;
    char name[1024];
} Client;

typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    int addrlen;
} SocketInfo;

// Define the message structure
typedef struct {
    int type;  // Message type
    char data[2048];  // Message data
} Message;

typedef struct {
    int socket;
    Message msg;
} ClientMsg;

typedef struct {
    char question[1024];
    char answer[1024];
} QA;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; 
QA questions[NUM_OF_QUESTIONS];
char curr_question[1024];
int curr_question_index = 0;
time_t curr_question_start_time;
int game_over_flag = 0;
char auth_code[7];

void print_participants() {
    printf("Participants:\n");
    for (int i = 0; i < client_count; i++) {
        printf("%s:%d - %s\n", inet_ntoa(clients[i].address.sin_addr), ntohs(clients[i].address.sin_port), clients[i].name);
    }
}

void create_shuffled_questions(FILE* file){
    // Read 5 lines (question) and 1 line (answer) from FILENAME
    for (int i = 0; i < NUM_OF_QUESTIONS; i++){
        questions[i].question[0] = '\0';
        for(int j = 0; j < 5; j++){ // Question + x4 Multi-Choice Answers
            char line[1024];
            if (fgets(line, sizeof(line), file) == NULL) {
                if (feof(file)) {
                    printf("Unexpected end of file\n");
                    fclose(file);
                    exit(EXIT_FAILURE);
                } else if (ferror(file)) {
                    perror("Error reading file");
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            }
            strcat(questions[i].question, line);
        }
        char line[1024];
        if (fgets(line, sizeof(line), file) == NULL) {
            if (feof(file)) {
                    printf("Unexpected end of file\n");
                    fclose(file);
                    exit(EXIT_FAILURE);
                } else if (ferror(file)) {
                    perror("Error reading file");
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
        }
        strcpy(questions[i].answer, line);
    }
    // Shuffle the questions
    for (int i = 0; i < NUM_OF_QUESTIONS; i++){
        int random_index = rand() % NUM_OF_QUESTIONS;
        QA temp = questions[i];
        questions[i] = questions[random_index];
        questions[random_index] = temp;
    }
    // Print the shuffled questions
    // for (int i = 0; i < NUM_OF_QUESTIONS; i++){
    //     printf("Question %d: %s", i + 1, questions[i].question);
    //     printf("Answer %d: %s", i + 1, questions[i].answer);
    // }
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

void generate_random_code() { // Generates a 6 characters code (a-z/A-Z/0-9)
    srand(time(NULL));
    for (int i = 0; i < 6; i++)
    {
        int type = rand() % 3;
        switch (type)
        {
        case 0:
            auth_code[i] = 'a' + rand() % 26;
            break;
        case 1:
            auth_code[i] = 'A' + rand() % 26;
            break;
        case 2:
            auth_code[i] = '0' + rand() % 10;
            break;
        }
    }
    auth_code[6] = '\0';
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
        // printf("Sent keep alive message\n");
        sleep(KEEP_ALIVE_INTERVAL);
        // Check if specific clients did not send keep alive messages
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < client_count; i++) {
            time_t current_time = time(NULL);
            if (current_time - clients[i].last_keep_alive_time > KEEP_ALIVE_TIMEOUT) {
                printf("%s did not send keep alive messages. Removing him from the game...\n", clients[i].name);
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

void* handle_client_msg(void* arg){
    ClientMsg* client_msg = (ClientMsg*)arg;
    Message* msg = &client_msg->msg;
    int sock = client_msg->socket;
    switch (msg->type)
    {
        case AUTH_SUCCESS:
            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < client_count; i++) {
                if (clients[i].socket == sock) {
                    printf("Client %s chose name %s\n", inet_ntoa(clients[i].address.sin_addr), msg->data);
                    if(strlen(msg->data) > 0) {
                        strcpy(clients[i].name, msg->data);
                    }
                    else {
                        strcpy(clients[i].name, inet_ntoa(clients[i].address.sin_addr));
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&client_mutex);
            break;
        case ANSWER:
            handle_client_answer(sock, msg->data);
            break;
        case KEEP_ALIVE:
            handle_keep_alive(sock);
            break;
        default:
            printf("Invalid message type %d\n", msg->type);
            break;
    }
    return NULL;
}

void* listen_for_messages(void* args){ 
    // Every Unicast message coming for a specific client 
    // will be handled in another thread
    int sock = *(int*)args;
    Message msg;
    msg.type = -1;
    ClientMsg client_msg;
    int bytes_receive_unicast = 0;
    while(1){
        memset(&msg, 0, sizeof(msg));
        if (client_count == 0){
            break;
        }
        bytes_receive_unicast = recv(sock, &msg, sizeof(msg), 0);
        if (bytes_receive_unicast == 0) { // Closed socket
            close(sock);
            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < client_count; i++) {
                if (clients[i].socket == sock) {
                    printf("%s disconnected\n", clients[i].name);
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
        // Check if a message is empty, if so - continue
        if (strlen(msg.data) == 0) {
            printf("Empty message received\n");
            continue;
        }
        client_msg.msg = msg;
        client_msg.socket = sock;
        pthread_t handle_message_thread;
        pthread_create(&handle_message_thread, NULL, handle_client_msg, (void*)&client_msg);
        pthread_detach(handle_message_thread);
    }
    return NULL;
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
    pthread_t listen_for_messages_thread;
    pthread_create(&listen_for_messages_thread, NULL, listen_for_messages, (void*)&socket);
    pthread_detach(listen_for_messages_thread);
    return NULL;
}

void* distribute_multicast_address(void* arg){ // Using unicast messages
    for (int i = 0; i < client_count; i++) {
        char multicast_address[1024];
        sprintf(multicast_address, "%s:%d", MULTICAST_IP, MULTICAST_PORT); // Creating the multicast address
        send_message(clients[i].socket, GAME_STARTING, multicast_address);
    }
    sleep(1);
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
            pthread_create(&thread_id, NULL, authenticate_client, (void*)&client_info);
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


void send_scoreboard(int multicast_sock, struct sockaddr_in multicast_addr) {
    char scoreboard[2048];
    sprintf(scoreboard, "\n==========Scoreboard==========\n");
    for (int i = 0; i < client_count; i++) {
        char client_score[2048];
        sprintf(client_score, "%s\t%d\n", clients[i].name, clients[i].score);
        strcat(scoreboard, client_score);
    }
    strcat(scoreboard, "\n\0");
    send_multicast_message(multicast_sock, multicast_addr, SCOREBOARD, scoreboard);
    printf("Current Scoreboard:\n%s", scoreboard);
}

void* send_questions(void* args){
    int num_questions = NUM_OF_QUESTIONS;
    while(1) {
        if (client_count == 0){
            break;
        }
        SocketInfo* info = (SocketInfo*)args;
        int multicast_sock = info->socket_fd;
        struct sockaddr_in multicast_addr = info->address;
        memset(curr_question, 0, sizeof(curr_question));
        strcpy(curr_question, questions[curr_question_index].question); 
        printf("\n%s\n", curr_question); // ! REMOVE?
        // Send the questions through multicast
        curr_question_start_time = time(NULL);
        send_multicast_message(multicast_sock, multicast_addr, QUESTION, curr_question);
        sleep(QUESTION_TIMEOUT);
        // send_multicast_message(multicast_sock, multicast_addr, ANSWER, "Time is up");
        sleep(QUESTION_TIMEOUT_BREAK);
        send_scoreboard(multicast_sock, multicast_addr);
        sleep(SCOREBOARD_BREAK);
        num_questions--;
        if (num_questions == 0){
            game_over_flag = 1;
            break;
        }
        curr_question_index++;
    }
    return NULL;
}

void handle_client_answer(int client_sock, char* client_answer) {
    time_t current_time = time(NULL);
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == client_sock) {
            if (strlen(client_answer) == 1){ 
                if (strncmp(client_answer, questions[curr_question_index].answer, 1) == 0) {    
                    // Compute elapsed time in seconds
                    int elapsed_time = (int)difftime(current_time, curr_question_start_time);
                    if (elapsed_time > QUESTION_TIMEOUT) {
                        send_message(client_sock, ANSWER, "Time is up");
                        break;
                    }
                    int curr_score = floor(30 / elapsed_time * 100 + elapsed_time);
                    printf("%s got %d points\n", clients[i].name, curr_score);
                    clients[i].score += curr_score;
                }
            }
            else {
                send_message(client_sock, INVALID, "Invalid Answer");
            }
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void* monitor_clients(void* args){
    pthread_t questions_thread = *(pthread_t*)args;
    while(1){
        if (client_count == 0 || game_over_flag){
            printf("No more clients connected. Ending game...\n");
            pthread_cancel(questions_thread);
            break;
        }
        sleep(1);
    }
    return NULL;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t thread_id;
    generate_random_code();

    // Create TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_BINDTODEVICE, INTERFACE_NAME, strlen(INTERFACE_NAME)) < 0) {
        perror("setsockopt(SO_BINDTODEVICE) failed");
        close(server_fd);
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

    FILE* questions_file = fopen(FILENAME, "r");
    create_shuffled_questions(questions_file);

    fclose(questions_file);
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN];
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            if (strcmp(ifa->ifa_name, INTERFACE_NAME) == 0) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            }
        }
    }
    printf("Connect to server with IP: %s and port: %d\n", ip, ntohs(address.sin_port));
    printf("Server started with authentication code %s\nWaiting for connections...\n", auth_code);

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
    int ttl = 10;
    setsockopt(multicast_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
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
    // for(int i = 0; i < client_count; i++){
    //     pthread_t listen_for_messages_thread;
    //     pthread_create(&listen_for_messages_thread, NULL, listen_for_messages, (void*)&clients[i].socket);
    //     pthread_detach(listen_for_messages_thread);
    // }

    pthread_t send_questions_thread;
    pthread_create(&send_questions_thread, NULL, send_questions, (void*)&multicast_info); // Multicast questions
    pthread_detach(send_questions_thread);

    pthread_t monitor_clients_thread;
    pthread_create(&monitor_clients_thread, NULL, monitor_clients, (void*)&send_questions_thread); // Multicast keep alive messages
    pthread_join(monitor_clients_thread, NULL);
    // Multicast Game-Over message
    send_multicast_message(multicast_sock, multicast_addr, GAME_OVER, "Game Over");
    
    printf("Game Over\n");
    // Kill the threads
    pthread_cancel(keep_alive_thread);
    pthread_cancel(deny_connections_thread);
    close(server_fd);
    close(multicast_sock);
    return 0;
}