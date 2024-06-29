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
#define AUTH_FAIL "Invalid authentication code"



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

int main() {
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
        // Set the socket back to blocking mode
        flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
        break;
    }
    char auth_buffer[1024];
    int auth_success = 0;
    while(!auth_success){
        memset(auth_buffer, 0, sizeof(auth_buffer));
        printf("Enter the authentication code: ");
        scanf("%s", auth_buffer);
        send(sock, auth_buffer, strlen(auth_buffer), 0);
        int bytes_received = recv(sock, auth_buffer, sizeof(auth_buffer), 0);
        auth_buffer[bytes_received] = '\0';
        if (strncmp(auth_buffer, AUTH_FAIL, 27) == 0) {
            printf("Invalid authentication code. Try again...\n");
        }
        else {
            printf("Authentication successful\n");
            auth_success = 1;
        }
    }

    fflush(stdin);
    send(sock, hello_msg, strlen(hello_msg), 0);
    printf("Ready message sent\n");

    pthread_t multicast_thread;
    //pthread_create(&multicast_thread, NULL, (void *)receive_multicast, NULL);

    while (1) {
        printf("Enter your answer: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;  // Remove newline character
        send(sock, buffer, strlen(buffer), 0);
    }

    close(sock);
    return 0;
}
