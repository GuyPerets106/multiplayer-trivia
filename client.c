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

#define SERVER_IP "172.16.223.136"
#define PORT 8080
#define MULTICAST_IP "228.6.73.122"
#define MULTICAST_PORT 12345

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

void send_authentication_code(int sock){
    char auth_buffer[1024];
    memset(auth_buffer, 0, sizeof(auth_buffer));
    printf("Enter the authentication code: ");
    scanf("%s", auth_buffer);
    send(sock, auth_buffer, strlen(auth_buffer), 0);
}

void receive_multicast() {
    printf("GOT HERE\n");
    int sock;
    struct sockaddr_in addr;
    struct ip_mreq mreq;
    char msgbuf[1024];

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
    addr.sin_port = htons(MULTICAST_PORT);

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind failure");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt failure");
        exit(1);
    }

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

int establish_connection(){
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

        // Set the socket to non-blocking mode for connect only
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            if (errno == EINPROGRESS) {
                struct timeval tv;
                tv.tv_sec = 1; // Change if needed
                tv.tv_usec = 0;
                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(sock, &fdset);
                int ret = select(sock + 1, NULL, &fdset, NULL, &tv); 
                if (ret < 0 && errno != EINTR) { 
                    printf("Error in select function, Try again...\n");
                    address_ok = 0;
                    fflush(stdin);
                    close(sock);
                    continue;
                }
                else if (ret == 0) { // Timeout
                    printf("Wrong IP or Port, Try again...\n");
                    address_ok = 0;
                    fflush(stdin);
                    close(sock);
                    continue;
                }
                else {
                    int so_error;
                    socklen_t len = sizeof(so_error);
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                    if (so_error) {
                        printf("Error connecting to server, Try again...\n");
                        address_ok = 0;
                        fflush(stdin);
                        close(sock);
                        continue;
                    }
                }
            } else {
                printf("Error connecting to server\n");
                address_ok = 0;
                fflush(stdin);
                close(sock);
                continue;
            }
        }
        address_ok = 1;
        return sock;
    }
}

void handle_message(Message msg, int client_socket) {
    switch (msg.type) {
        case AUTH_FAIL:
            printf("Authentication failed: '%s'\n", msg.data);
            send_authentication_code(client_socket);
            break;
        case AUTH_SUCCESS:
            printf("Authentication successful\n");
            break;
        case MAX_TRIES:
            printf("Maximum number of tries exceeded\n");
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
        default:
            printf("Unknown message type: %d\n", msg.type);
            break;
    }
}

int main() {
    int sock = establish_connection(); // DONE When IP and Port are correct
    send_authentication_code(sock);
    
    Message msg_unicast, msg_multicast;
    int bytes_receive_unicast, bytes_receive_multicast;
    // Main Loop 
    while(1){
        // Receive in non-blocking mode
        bytes_receive_unicast = recv(sock, &msg_unicast, sizeof(msg_unicast), MSG_DONTWAIT);
        bytes_receive_multicast = recv(sock, &msg_unicast, sizeof(msg_multicast), MSG_DONTWAIT);

        // handle message (UNICAST)
        if (bytes_receive_unicast > 0) {
            handle_message(msg_unicast, sock);
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
            break;
        }

        // handle message (MULTICAST)
        if (bytes_receive_multicast > 0) {
            handle_message(msg_unicast, sock);
        }
        else if (bytes_receive_multicast == 0) { // Socket closed
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
            break;
        }
    }
    
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

    close(sock);
    return 0;
}
