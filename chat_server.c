#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

#define Buffer_size 1024
#define maxLen 256
//prototype
int createWelcomeSocket(short port, int maxClient);
int exitAll(int maxOpen, int server, int* client, char** user, struct sockaddr_in* addr,char* data);

int main( int argc, char *argv[] )  {

    if( argc ==3 ) {
        int welcomeSocket,len, maxClient=(int)atoi(argv[2]),i=0,j=0, numOfByte=0,numOfconnect=0,index=0;
        int *clientSocket, fullFlag=0;
        short portNum=(short)atoi(argv[1]);
        char buffer[Buffer_size];
        char **userName,*data=NULL;
        struct sockaddr_in *clientAddr;
        struct sockaddr_in welcomeAddr;
        fd_set current_socket, ready_socket;
        socklen_t welcome_size, client_size;

        memset(welcomeAddr.sin_zero, 0, sizeof welcomeAddr.sin_zero);

        userName=(char**)malloc(maxClient*sizeof(char *));
        clientAddr=(struct sockaddr_in*)malloc((maxClient * (sizeof(struct sockaddr_in))));
        clientSocket= (int*)malloc(maxClient*(sizeof(int)));
        data=(char*)malloc((maxLen+1)* sizeof(char));

        for(i=0;i<maxClient;i++){
            userName[i]=(char*) malloc(7* sizeof(char));
            if(userName[i]==NULL){
                for(j=0;j<i;j++){
                    free(userName[j]);
                }
            }
        }
        for(i=0;i<maxClient;i++){
            clientSocket[i]=0;
        }

        if(clientAddr==NULL||clientSocket==NULL||userName==NULL||data==NULL){
            perror("malloc failed");
            exit(EXIT_FAILURE);
        }

        welcomeSocket= createWelcomeSocket(portNum, maxClient);
        if(welcomeSocket<0){
            exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
        }

        FD_ZERO(&current_socket);
        FD_SET(welcomeSocket,&current_socket);
        FD_SET(fileno(stdin),&current_socket);

        while(1) {
            ready_socket= current_socket; // ! HERE ready_socket = tmp_fds , current_socket = read_fds
            if(select(FD_SETSIZE, &ready_socket,NULL,NULL,NULL)<0){
                perror("select failed");
                exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
                exit(EXIT_FAILURE);
            }
            if(FD_ISSET(fileno(stdin),&ready_socket)){  //if we want to shutdown the server
                printf("Bye Bye...\n");
                exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
                return 0;
            }
            for(i=0;i<FD_SETSIZE;i++){
                if(FD_ISSET(i,&ready_socket)){
                    if(i == welcomeSocket){  //if new connection to the welcome socket
                        if((numOfconnect==maxClient)&&!fullFlag){  //if we have maxConneted, we close the welcome socket
                            close(welcomeSocket);
                            FD_CLR(welcomeSocket,&current_socket);
                            fullFlag=1;     //to use later when we want to re-open the socket
                        }
                        if(!fullFlag){   //if we have free connection, we accept the new one
                            index=0;
                            while(index<maxClient&&clientSocket[index]!=0) ++index; //find place for the new one
                            client_size= sizeof(clientAddr[index]);
                            memset(&clientAddr[index],0,client_size);
                            if((clientSocket[index]= accept(welcomeSocket,(struct sockaddr*)&clientAddr[index],&client_size))<0){
                                perror("accept failed");
                                exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
                                exit(EXIT_FAILURE);
                            }
                            FD_SET((clientSocket[index]),&current_socket);
                            numOfconnect++;
                        }
                    } else {   //if it's not the welcomeSocket we need to handle the message
                        if((numOfByte= recv(i,buffer,Buffer_size,0))<0){
                            perror("reading failed");
                            exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
                            exit(EXIT_FAILURE);
                        } else if(numOfByte==0){  //if the client left the server
                            index=0;
                            while (index<numOfconnect&&clientSocket[index]!=i) ++index;
                            printf("%s has left the server...\n",userName[index]);
                            close(clientSocket[index]);
                            clientSocket[index]=0;
                            numOfconnect--;
                            FD_CLR(i,&current_socket);
                            if((numOfconnect==(maxClient-1))&&fullFlag){ //if we had max connection and someone get out, we re-open the welcome socket
                                welcomeSocket= createWelcomeSocket(portNum,maxClient);
                                if(welcomeSocket<1){
                                    exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
                                    exit(EXIT_FAILURE);
                                }

                                FD_SET(welcomeSocket,&current_socket);
                                fullFlag=0;
                            }
                        } else {    //handle the first message of the user
                            index=0;
                            while (index<numOfconnect&&clientSocket[index]!=i) ++index;
                            sprintf(userName[index],"%6s",buffer);
                            userName[index][6]='\0';
                            len=(16*buffer[7]+buffer[6]);
                            memset(data,0,maxLen);
                            for(j=0;j<len;j++){
                                data[j]=buffer[8+j];
                            }
                            data[len]='\0';
                            printf("%s has connected to the server.\n",userName[index]);
                            memset(buffer,0,Buffer_size);
                            sprintf(buffer,"User: %s has said: %s \n",userName[index],data);
                            for(j=0;j<maxClient;j++){
                                if((clientSocket[j]!=i)&&(clientSocket[j]!=0)){  //send the data to all other client
                                    if(send(clientSocket[j],buffer,Buffer_size,0)<0){
                                        perror("send failed");
                                        exitAll(maxClient,welcomeSocket,clientSocket,userName,clientAddr,data);
                                        exit(EXIT_FAILURE);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    }  else {
        printf("2 argument expected.\n");
        return -1;
    }
}
int exitAll(int maxOpen, int server, int* client, char** user, struct sockaddr_in* addr,char* data){
    int k=0;
    for(k-0;k<maxOpen;k++){
        if(client[k]!=0){
            close(client[k]);
        }
        free(user[k]);
    }
    close(server);
    free(client);
    free(addr);
    free(user);
    free(data);
    return 0;
}

int createWelcomeSocket(short port, int maxClient){
    int serverSocket, opt=1;
    struct sockaddr_in serverAddr;
    socklen_t server_size;

    serverSocket= socket(PF_INET,SOCK_STREAM,0);
    if(serverSocket<0){
        perror("socket failed");
        return -1;
    }
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,&opt, sizeof(opt))){
        perror("socket option failed");
        close(serverSocket);
        return -1;
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    server_size= sizeof(serverAddr);

    if((bind(serverSocket,(struct sockaddr *)&serverAddr,server_size))<0) {
        perror("binding failed");
        close(serverSocket);
        return -1;
    }

    printf("Server is listen to port %d and wait for new client...\n", port);

    if((listen(serverSocket,maxClient))<0){
        perror("listen failed");
        close(serverSocket);
        return -1;
    }
    return serverSocket;
}

