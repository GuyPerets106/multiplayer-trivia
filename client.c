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
#define GAME_OVER 9
#define INVALID 10

int game_started = 0;
pthread_cond_t cond;


void* handle_message(void* args);

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

pthread_t curr_question_thread;
char curr_answer[1024];

void send_message(int sock, int msg_type, const char *msg_data) {
    Message msg;
    msg.type = msg_type;
    strncpy(msg.data, msg_data, sizeof(msg.data) - 1);
    msg.data[sizeof(msg.data) - 1] = '\0';  // Ensure null-termination

    // Send the message
    send(sock, &msg, sizeof(msg), 0);
}

void send_authentication_code(int sock){
    char auth_buffer[1024];
    memset(auth_buffer, 0, sizeof(auth_buffer));
    printf("Enter the authentication code: ");
    scanf("%s", auth_buffer);
    send(sock, auth_buffer, strlen(auth_buffer), 0);
}

void receive_multicast(int sock) {
    char msgbuf[1024];
    while (1) {
        int nbytes = recvfrom(sock, msgbuf, sizeof(msgbuf), 0, NULL, NULL);
        if (nbytes < 0) {
            perror("recvfrom");
            exit(1);
        }
        msgbuf[nbytes] = '\0';
        printf("Received question: %s\n", msgbuf);
    }
}

void answer_question() {
    printf("Enter your answer: ");
    while(1){
        fd_set set;
        struct timeval timeout;

        // Initialize the file descriptor set
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        // Initialize the timeout data structure
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000; // 500ms

        // Check if there's input on stdin
        int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
        if (rv == -1) {
            perror("select"); // Error occurred in select()
        } else if (rv == 0) {
            continue;
        } else {
            // Data is available, perform non-blocking read
            if (FD_ISSET(STDIN_FILENO, &set)) {
                scanf("%s", curr_answer);
                break;
            }
        }
    }
    fflush(stdin);
}

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
            fflush(stdin);
            continue;
        }
        else {
            address_ok = 1;
            fflush(stdin);
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
}


void* handle_unicast(void* args){ // Handles first connections with the server and authentication
    int bytes_receive_unicast;
    Message msg_unicast;
    int sock = *(int*)args;
    while(1){ // Unicast
        memset(msg_unicast.data, 0, sizeof(msg_unicast.data));
        bytes_receive_unicast = recv(sock, &msg_unicast, sizeof(msg_unicast), 0); // ! BLOCKING
        if (bytes_receive_unicast > 0) {
            pthread_t handle_unicast_msg;
            MessageThreadArgs* thread_args = (MessageThreadArgs*)malloc(sizeof(MessageThreadArgs));
            thread_args->socket = sock;
            thread_args->msg = msg_unicast;
            // printf("Message received: %s\n", msg_unicast.data);
            pthread_create(&handle_unicast_msg, NULL, handle_message, (void*)thread_args);
            pthread_join(handle_unicast_msg, NULL);
            continue; // ! REMOVE ALL CONTINUES WHEN MULTICAST IS IMPLEMENTED
        }
        else if (bytes_receive_unicast == 0) { // Socket closed
            printf("Server disconnected\n");
            fflush(stdin);
            // stdin should be blocking
            fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);
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
            MessageThreadArgs* thread_args = (MessageThreadArgs*)malloc(sizeof(MessageThreadArgs));
            thread_args->socket = unicast_sock;
            thread_args->msg = msg_multicast;
            // printf("Message received: %s\n", msg_multicast.data);
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
    free(args);
    return NULL;
}

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
    MulticastThreadArgs* args = (MulticastThreadArgs*)malloc(sizeof(MulticastThreadArgs));
    args->multicast_socket = sock;
    args->unicast_socket = unicast_sock;
    args->addr = (struct sockaddr*)&addr;
    pthread_t handle_multicast_thread;
    pthread_create(&handle_multicast_thread, NULL, handle_multicast, (void*)args);
    pthread_detach(handle_multicast_thread);
}

void* handle_message(void* args) {
    MessageThreadArgs* thread_args = (MessageThreadArgs*)args;
    Message msg = thread_args->msg;
    int client_socket = thread_args->socket;
    switch (msg.type) {
        case AUTH_FAIL:
            printf("Authentication Failed: '%s'\n", msg.data);
            send_authentication_code(client_socket);
            break;
        case AUTH_SUCCESS:
            printf("Authentication Successful\n");
            printf("Choose your game name: ");
            char username[1024];
            scanf("%s", username);
            send_message(client_socket, AUTH_SUCCESS, username);
            printf("Waiting for the game to start...\n");
            break;
        case MAX_TRIES:
            printf("Maximum number of tries exceeded, disconnecting...\n\n");
            close(client_socket);
            fflush(stdin);
            client_socket = establish_connection(); // BLOCKING
            send_authentication_code(client_socket);
            break;
        case GAME_STARTED:
            printf("%s. Go fuck yourself...\n\n", msg.data);
            close(client_socket);
            fflush(stdin);
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
            printf("%s", msg.data);
            curr_question_thread = pthread_self(); // ! Consider Mutex
            answer_question();
            printf("My Answer: %s\n", curr_answer);
            send_message(client_socket, ANSWER, curr_answer);
            break;
        case ANSWER: // ! Receive Unicast When Timeout
            pthread_cancel(curr_question_thread);
            int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
            fflush(stdin);
            printf("Got timeout for answer");
            break;
        case SCOREBOARD:
            printf("Got Scoreboard\n");
            printf("%s\n", msg.data);
            break;
        case GAME_OVER:
            printf("Game Over\n");
            close(client_socket);
            fflush(stdin);
            client_socket = establish_connection(); // ? Close the program?
            send_authentication_code(client_socket);
            break;
        case INVALID:
            printf("Invalid Answer\n");
            curr_question_thread = pthread_self(); // ! Consider Mutex
            answer_question();
            printf("My Answer: %s\n", curr_answer);
            send_message(client_socket, ANSWER, curr_answer);
            break;
        default:
            printf("Unknown message type: %d\n", msg.type);
            break;
    }
    free(args);
    return NULL;
}

int main() {
    pthread_cond_init(&cond, NULL);
    int sock = establish_connection(); // DONE When IP and Port are correct
    send_authentication_code(sock);
    
    
    pthread_t handle_unicast_thread;
    pthread_create(&handle_unicast_thread, NULL, handle_unicast, (void*)&sock);
    pthread_detach(handle_unicast_thread);
    while(1);
    
    // fflush(stdin);
    // send(sock, hello_msg, strlen(hello_msg), 0);
    // printf("Ready message sent\n");

    // pthread_t multicast_thread;
    // //pthread_create(&multicast_thread, NULL, (void *)receive_multicast, NULL);

    // while (1) {
    //     printf("Enter your answer: ");
    //     fgets(buffer, sizeof(buffer), stdin);
    //     buffer[strcspn(buffer, "\n")] = 0;  // Remove newline character
    //     send(sock, buffer, strlen(buffer), 0);
    // }
    printf("Exiting...\n");
    return 0;
}
