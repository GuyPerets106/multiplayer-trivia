// Compile with: gcc client.c -o client

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>


#define AUTH_SUCCESS_MSG "Authentication successful"
#define AUTH_FAIL_MSG "Invalid authentication code"
#define MAX_TRIES_MSG "Maximum number of tries exceeded"
#define GAME_STARTED_MSG "Game already started"
#define KEEP_ALIVE_MSG "Keep alive"
#define QUESTION_TIMEOUT 10

#define AUTH_SUCCESS 1
#define AUTH_FAIL 2
#define MAX_TRIES 3
#define GAME_STARTED 4
#define GAME_STARTING 5
#define QUESTION 6
#define ANSWER 7
#define KEEP_ALIVE 8
#define SCOREBOARD 9
#define GAME_OVER 10
#define INVALID 11

int game_started = 0;

void* handle_message(void* args);

void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        printf("Question timout reached\n");
    }
    return;
}

// Define the message structure
typedef struct {
    int type;  // Message type
    char data[1024];  // Message data
} Message;

typedef struct {
    int socket;
    Message msg;
} MessageThreadArgs;

typedef struct {
    int multicast_socket;
    int unicast_socket;
    struct sockaddr * addr;
} MulticastThreadArgs;

typedef struct {
    char ip[16];
    int port;
} MulticastAddress;

pthread_mutex_t lock_answer = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_question = PTHREAD_MUTEX_INITIALIZER;
pthread_t curr_question_thread;
char curr_answer[1024];

// establish_connection() function: Open Socket, ask client for IP and Port, then connect to the server, return the socket
int establish_connection(){
    // printf("GOT HERE\n");
    int sock = 0;
    struct sockaddr_in serv_addr;
    char *hello_msg = "Ready";
    char buffer[1024] = {0};
    char server_ip[16];
    int server_port;
    int address_ok = 0;

    while((sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {
        fflush(stdin);
        if (!address_ok) {
            printf("Enter the IP address of the server: ");
            scanf("%s", server_ip);
            printf("Enter the port number of the server: ");
            scanf("%d", &server_port);
        }
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port);
        if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
            printf("\n**Invalid address**\n");
            close(sock);
            continue;
        }
        else {
            address_ok = 1;
        }

        if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            if(errno == ECONNREFUSED) {
                printf("Wrong IP or Port. Try again...\n");
                address_ok = 0;
                continue;
            }
            else {
                perror("Connection failed");
                address_ok = 0;
                continue;
            }
        }
        else {
            return sock;
        }
    }
    return -1;
}

// send_authentication_code() function: Ask the client for the authentication code(Blocking!), then send it to the server using the socket
void send_authentication_code(int sock){
    char auth_buffer[1024];
    memset(auth_buffer, 0, sizeof(auth_buffer));
    printf("Enter the authentication code: ");
    scanf("%s", auth_buffer);
    send(sock, auth_buffer, strlen(auth_buffer), 0);
}

// handle_unicast() function: always running, on recv(), if the server sends a message, create a new thread to handle the message
void* handle_unicast(void* args){ // Handles first connections with the server and authentication
    int bytes_receive_unicast;
    Message msg_unicast;
    int sock = *(int*)args;
    while(1){ // Unicast
        memset(msg_unicast.data, 0, sizeof(msg_unicast.data));
        bytes_receive_unicast = recv(sock, &msg_unicast, sizeof(msg_unicast), 0); // ! BLOCKING
        if (bytes_receive_unicast > 0) {
            pthread_t handle_unicast_msg;
            MessageThreadArgs* thread_args = malloc(sizeof(MessageThreadArgs));
            thread_args->socket = sock;
            thread_args->msg = msg_unicast;
            // printf("Message received: %s\n", msg_unicast.data);
            pthread_create(&handle_unicast_msg, NULL, handle_message, (void*)thread_args);
            pthread_detach(handle_unicast_msg);
            continue; // ! REMOVE ALL CONTINUES WHEN MULTICAST IS IMPLEMENTED
        }
        else if (bytes_receive_unicast == 0) { // Socket closed
            printf("Server disconnected\n");
            sock = establish_connection();
            send_authentication_code(sock);
            continue;
        }
        else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // No message received, do something else
            continue;
        }
        else {
            perror("Error in recv function");
            printf("Message received: %s\n", msg_unicast.data);
            break;
        }
    }
    return NULL;
}

// handle_message() types:
// #1 AUTH_FAIL: Print the message and ask for the authentication code again(by calling send_authentication_code())
// #2 AUTH_SUCCESS: Print the message and ask for the game name, then sends name to server using send_message // ! game name can be function(DO)
// #3 MAX_TRIES: close the socket, establish a new connection(by establish_connection), and send the authentication code(by send_authentication_code)
// #4 GAME_STARTED: if the game has already started, close the socket, try to establish connection with different IP and Port, and send the authentication code
// #5 GAME_STARTING: if client got the GAME_STARTING message, open a multicast socket(by calling open_multicast_socket())
// #6 KEEP_ALIVE: send a KEEP_ALIVE message to the server, using send_message()
// #7 QUESTION: print the question, and ask for the answer(by calling answer_question()), then send the answer to the server by send_message()
// #8 ANSWER: // ! Look at the function(what the hell is this?)
// #9 SCOREBOARD: print the scoreboard that got from server as msg.data
// #10 GAME_OVER: print "Game Over", close the socket, establish a new connection, and send the authentication code
void* handle_message(void* args) {
    signal(SIGUSR1, handle_signal);
    MessageThreadArgs* thread_args = (MessageThreadArgs*)args;
    Message msg = thread_args->msg;
    int client_socket = thread_args->socket;
    int name_flag = 1;
    switch (msg.type) {
        case AUTH_FAIL:
            printf("Authentication Failed: '%s'\n", msg.data);
            send_authentication_code(client_socket);
            break;
        case AUTH_SUCCESS:
            printf("Authentication Successful\n");
            printf("Choose your game name:\n");
            char username[1024];
            fd_set readfds;
            while(name_flag){
                struct timeval tv;
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                int ret;
                FD_ZERO(&readfds);
                FD_SET(0, &readfds);
                // Use select to see if a game name is entered
                // If not, send ''
                ret = select(1, &readfds, NULL, NULL, &tv);
                if(ret == -1){
                    perror("select");
                    exit(1);
                }
                else if(ret){
                    scanf("%s", username);
                    name_flag = 0;
                }
                else{
                    printf("No game name entered, using your IP address...\n");
                    name_flag = 0;
                }
            }
            send_message(client_socket, AUTH_SUCCESS, username);
            printf("Waiting for the game to start...\n");
            break;
        case MAX_TRIES:
            printf("Maximum number of tries exceeded, disconnecting...\n");
            close(client_socket);
            client_socket = establish_connection(); // BLOCKING
            send_authentication_code(client_socket);
            break;
        case GAME_STARTED:
            printf("%s. Go fuck yourself...\n", msg.data);
            close(client_socket);
            client_socket = establish_connection(); // BLOCKING
            send_authentication_code(client_socket);
            break;
        case GAME_STARTING: // Receive Unicast
            game_started = 1;
            printf("Got multicast address %s from server, opening the multicast socket...\n", msg.data);
            open_multicast_socket(client_socket, msg.data);
            break;
        case KEEP_ALIVE: // Receive Multicast
            // printf("Got keep alive message from the server\n");
            send_message(client_socket, KEEP_ALIVE, KEEP_ALIVE_MSG); // Send Unicast
            break;
        case QUESTION: // Receive Multicast
            memset(curr_answer, 0, sizeof(curr_answer));
            printf("%s", msg.data);
            pthread_mutex_lock(&lock_question);
            curr_question_thread = pthread_self(); // ! Consider Mutex
            pthread_mutex_unlock(&lock_question);
            answer_question();
            if (strlen(curr_answer) == 0) {
                break;
            }
            else {
                send_message(client_socket, ANSWER, curr_answer);
            }
            break;
        case ANSWER: // ! Receive Unicast When Timeout
            // ? DEPRECATED
            pthread_mutex_lock(&lock_question);
            pthread_kill(curr_question_thread, SIGUSR1);
            pthread_mutex_unlock(&lock_question);
            break;
        case SCOREBOARD:
            printf("%s", msg.data);
            break;
        case GAME_OVER:
            printf("Game Over\n");
            close(client_socket);
            client_socket = establish_connection(); // ? Close the program?
            send_authentication_code(client_socket);
            break;
        case INVALID:
            printf("Invalid Answer\n");
            pthread_mutex_lock(&lock_question);
            curr_question_thread = pthread_self(); // ! Consider Mutex
            pthread_mutex_unlock(&lock_question);
            answer_question();            
            send_message(client_socket, ANSWER, curr_answer);
            break;
        default:
            printf("Unknown message type: %d\n", msg.type);
            break;
    }
    free(thread_args);
    return NULL;
}

// send_message() function: send a message to the server using the socket
void send_message(int sock, int msg_type, const char *msg_data) {
    pthread_mutex_lock(&lock_answer);
    Message msg;
    memset(msg.data, 0, sizeof(msg.data));
    msg.type = msg_type;
    strncpy(msg.data, msg_data, strlen(msg_data));
    msg.data[strlen(msg_data)] = '\0';  // Ensure null-termination

    // Send the message
    // printf("Sending message %d: %s\n", msg.type, msg.data);
    send(sock, &msg, sizeof(msg), 0);
    pthread_mutex_unlock(&lock_answer);
}

// open_multicast_socket() function: open a multicast socket, bind it to the multicast address, and open a new thread to handle the multicast messages, handle_multicast()
void open_multicast_socket(int unicast_sock, char* msg){
    MulticastAddress multicast_address;
    char splitter[] = ":";
    char* token = strsep(&msg, splitter);
    strcpy(multicast_address.ip, token);
    token = strsep(&msg, splitter);
    multicast_address.port = atoi(token);

    int sock;
    struct sockaddr_in addr;
    struct ip_mreq mreq;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failure");
        exit(1);
    }

    u_int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("Reusing ADDR failed");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(multicast_address.port);

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind failure");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = inet_addr(multicast_address.ip);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt failure");
        exit(1);
    }
    printf("Listening to multicast address %s:%d\n", multicast_address.ip, multicast_address.port);
    MulticastThreadArgs* args = malloc(sizeof(MulticastThreadArgs));
    args->multicast_socket = sock;
    args->unicast_socket = unicast_sock;
    args->addr = (struct sockaddr*)&addr;
    pthread_t handle_multicast_thread;
    pthread_create(&handle_multicast_thread, NULL, handle_multicast, (void*)args);
    pthread_detach(handle_multicast_thread);
}

// handle_multicast() function: always running, on recv(), if the server sends a message, create a new thread to handle the message, handle_message()
void* handle_multicast(void* args){
    int bytes_receive_multicast;
    Message msg_multicast;
    MulticastThreadArgs* thread_args = (MulticastThreadArgs*)args;
    int multicast_sock = thread_args->multicast_socket;
    int unicast_sock = thread_args->unicast_socket;
    struct sockaddr* addr = thread_args->addr;
    socklen_t addrlen = sizeof(*addr);
    while(game_started){
        memset(msg_multicast.data, 0, sizeof(msg_multicast.data));
        bytes_receive_multicast = recvfrom(multicast_sock, &msg_multicast, sizeof(msg_multicast), 0, addr, &addrlen); // ! BLOCKING
        if (bytes_receive_multicast > 0) {
            pthread_t handle_multicast_msg;
            MessageThreadArgs* thread_args = malloc(sizeof(MessageThreadArgs));
            thread_args->socket = unicast_sock;
            thread_args->msg = msg_multicast;
            pthread_create(&handle_multicast_msg, NULL, handle_message, (void*)thread_args);
            pthread_detach(handle_multicast_msg);
            continue;
        }
        else if (bytes_receive_multicast == 0) { // Socket closed
            printf("Multicast socket closed\n");
            break;
        }
        else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // No message received, do something else
            continue;
        }
        else {
            perror("Error in recv function");
            printf("Message received: %s\n", msg_multicast.data);
            break;
        }
    }
    free(thread_args);
    return NULL;
}

// answer_question() function: ask the client for the answer(Blocking), then store it in curr_answer(public variable)
void answer_question() {
    printf("Enter your answer:\n");
    pthread_mutex_lock(&lock_answer);
    // Use select on stdin
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = QUESTION_TIMEOUT;
    tv.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_SET(fileno(stdin), &readfds);
    int ret = select(1, &readfds, NULL, NULL, &tv);
    if(ret == -1){
        perror("select");
        exit(1);
    }
    else if(FD_ISSET(fileno(stdin), &readfds)){
        scanf("%s", curr_answer);
    }
    else{
        printf("Question Timeout Reached...\n");
        fflush(stdin);
    }
    pthread_mutex_unlock(&lock_answer);
}

int main() {
    int sock;
    sock = establish_connection(); // DONE When IP and Port are correct
    send_authentication_code(sock);
    
    pthread_t handle_unicast_thread;
    pthread_create(&handle_unicast_thread, NULL, handle_unicast, (void*)&sock);
    pthread_detach(handle_unicast_thread);
    while(1);
    printf("Exiting...\n");
    return 0;
}
