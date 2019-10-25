#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define ERROR -1    //defining error message
#define MAX_BUFFER_SIZE 512    //setting maximum size of buffer for sending and receiving messages/key only
#define PORT "SERVER PORT HERE"   //defining port
#define MAX_CLIENTS 8
#define MAX_FILES   30
#define TOTAL_CHUNKS 8

struct file_info
{
    char file_name[MAX_BUFFER_SIZE];    //file name
    int file_size;  //file size
    int file_chunk[8]; //each file will have 8 chunks and 0 means chunk not present and 1 means chunk present
};

struct client_info
{
    uint32_t client_ip; //client ip
    uint16_t client_listening_port;   //client port
    int total_files;    //total number of files
    int next_available_file;    //index of files this client has
};

struct thread_arg
{
    int new_client_sock;
    int client_id;
};

struct client_info clients[MAX_CLIENTS];    //array of clients in the network
int next_available_client=0;    //index of next available client
struct file_info clients_file_info[MAX_CLIENTS][MAX_FILES]; //array of files of each client in the network
struct client_info client_data;
struct file_info client_file;
pthread_mutex_t lock;   //to handle race conditions



//print functions
void print_metadata(){
    printf("next_available_client: %d\n",next_available_client);
    for (int i=0;i<next_available_client;i++){
        printf("client ip: %lu, client_listening_port: %u, total files: %d,next_available_file: %d\n",(unsigned long)clients[i].client_ip,(unsigned)clients[i].client_listening_port,clients[i].total_files,clients[i].next_available_file);
        for (int j=0;j<clients[i].next_available_file;j++){
            for (int k=0;k<TOTAL_CHUNKS;k++){
                printf("file_name: %s, file_size: %d, file_chunk: %d\n",clients_file_info[i][j].file_name,clients_file_info[i][j].file_size,clients_file_info[i][j].file_chunk[k]);
            }
        }
    }
}
//

void send_metadata(int sock){
    int curr_len;
    int len;
    pthread_mutex_lock(&lock);
    int temp=htonl(next_available_client);
    len=write(sock,(void *)&temp,sizeof(temp));    //send next available client integer
    
    len=write(sock,(void *)clients,sizeof(clients));   //send clients
    curr_len=len;
    while (curr_len!=sizeof(clients)){
        len=write(sock,(void *)clients+curr_len,sizeof(clients)-curr_len);
        curr_len=curr_len+len;
    }
    
    len=write(sock,(void *)clients_file_info,sizeof(clients_file_info));   //send clients file info
    curr_len=len;
    while (curr_len!=sizeof(clients_file_info)){
        len=write(sock,(void *)clients_file_info,sizeof(clients_file_info)-curr_len);
        curr_len+=len;
    }
    
    pthread_mutex_unlock(&lock);
    // print_metadata();
}

void rec_metadata(int sock){
    int curr_len;
    int len;
    pthread_mutex_lock(&lock);
    len=recv(sock,(void *)&next_available_client,sizeof(int),0);    //recv next available client integer
    len=recv(sock,(void *)clients,sizeof(clients),0);   //recv clients
    curr_len=len;
    while (curr_len!=sizeof(clients)){
        len=recv(sock,(void *)clients+curr_len,sizeof(clients)-curr_len,0);
        curr_len+=len;
    }
    len=recv(sock,(void *)clients_file_info,sizeof(clients_file_info),0);   //recv clients_file_info
    curr_len=len;
    while (curr_len!=sizeof(clients_file_info)){
        len=recv(sock,(void *)clients_file_info+curr_len,sizeof(clients_file_info)-curr_len,0);
        curr_len+=len;
    }
    pthread_mutex_unlock(&lock);
    // print_metadata();
}

void *service_provider(void *arg){
    struct thread_arg *args=(struct thread_arg *)arg;
    int len;    //receive length bytes
    char senddata[MAX_BUFFER_SIZE];
    char recdata[MAX_BUFFER_SIZE];
    char service_key[MAX_BUFFER_SIZE];
    int new_client_sock=args->new_client_sock;
    int client_id=args->client_id;
    
    send(new_client_sock,(void *)&client_id,sizeof(client_id),0);   //sending client id
    
    //rec listening port
    len=recv(new_client_sock,(void *)&clients[client_id].client_listening_port,sizeof(clients[client_id].client_listening_port),0);
    clients[client_id].next_available_file=0;
    clients[client_id].total_files=0;
    
    // start receiving service reqs
    while(1){
        len=recv(new_client_sock,(void *)service_key,MAX_BUFFER_SIZE,0);    //rec search key
        service_key[len]='\0';
        printf("%s\n",service_key);  //printing message received from client
        if (service_key[0]=='u' && service_key[1]=='p'){
            //rec total files
            len=recv(new_client_sock,(void *)&clients[client_id].total_files,sizeof(clients[client_id].total_files),0);
            for (int i=0; i<clients[client_id].total_files;i++){
                //recv file info
                bzero(recdata,MAX_BUFFER_SIZE);
                len=recv(new_client_sock,(void *)recdata,MAX_BUFFER_SIZE,0);
                strcpy(clients_file_info[client_id][clients[client_id].next_available_file].file_name,recdata);
                len=recv(new_client_sock,(void *)&clients_file_info[client_id][clients[client_id].next_available_file].file_size,sizeof(clients_file_info[client_id][clients[client_id].next_available_file].file_size),0);
                len=recv(new_client_sock,(void *)clients_file_info[client_id][clients[client_id].next_available_file].file_chunk,sizeof(clients_file_info[client_id][clients[client_id].next_available_file].file_chunk),0);
                clients[client_id].next_available_file++;
            }
            // print_metadata();
            char message[]="FILE/S UPLOADED SUCCESSFULLY!!!";
            send(new_client_sock,(void *)message,sizeof(message),0);    //sending message
            bzero(recdata,MAX_BUFFER_SIZE);
        } else if (service_key[0]=='s' && service_key[1]=='e'){
            bzero(recdata,MAX_BUFFER_SIZE);
            // print_metadata();
            len=recv(new_client_sock,(void *)recdata,MAX_BUFFER_SIZE,0);    //rec file name
            recdata[len]='\0';
            printf("%s\n",recdata); //printing filename the user asked for
            //send everything
            send_metadata(new_client_sock);
            bzero(recdata,MAX_BUFFER_SIZE);
        } else if (service_key[0]=='u' && service_key[1]=='d'){
            printf("UPDATING SERVER!!!\n");
            rec_metadata(new_client_sock);
        } else if (service_key[0]=='e' && service_key[1]=='x'){
            bzero(recdata,MAX_BUFFER_SIZE);
            printf("CLIENT LEFT THE SERVER!!!\n");
            close(new_client_sock);
            // free(args);
            break;
        } else if (service_key[0]=='d' && service_key[1]=='n'){
            bzero(recdata,MAX_BUFFER_SIZE);
            // print_metadata();
            len=recv(new_client_sock,(void *)recdata,MAX_BUFFER_SIZE,0);    //rec file name
            recdata[len]='\0';
            printf("%s\n",recdata); //printing filename the user asked for
            //send everything
            send_metadata(new_client_sock);
            bzero(recdata,MAX_BUFFER_SIZE);
            rec_metadata(new_client_sock);
            // print_metadata();
        } else if (service_key[0]=='r' && service_key[1]=='e'){
            printf("REFRESHING CLIENT!!!\n");
            send_metadata(new_client_sock);
        } else {
            break;
        }
    }
    return NULL;
}

int main(int argc, char **argv){
    int sock;   //socket descriptor for server
    struct sockaddr_in server;  //server structure
    int new_client; //socket descriptor for new client
    struct sockaddr_in client;  //new client structure
    int sockaddr_len=sizeof(struct sockaddr_in);    //stores length of sockaddr

    //serving clients
    pthread_t new_client_thread[MAX_CLIENTS];
    int next_available_client_thread=0;
    struct thread_arg *args=(struct thread_arg *)malloc(sizeof(struct thread_arg));

    //get socket descriptor
    sock=socket(AF_INET,SOCK_STREAM,0);
    if (sock==ERROR){
        perror("server socket error");
        exit(-1);
    }

    //server structure
    server.sin_family=AF_INET;
    server.sin_port=htons(PORT);
    server.sin_addr.s_addr=INADDR_ANY;
    bzero(&server.sin_zero,8);  //padding zeroes using bzero function from strings.h

    //binding socket
    if ((bind(sock,(struct sockaddr *)&server,sockaddr_len))==ERROR){
        perror("bind");
        exit(-1);
    }

    if (listen(sock,MAX_CLIENTS)==ERROR){
        perror("listen");
        exit(-1);
    }

    if (pthread_mutex_init(&lock, NULL) != 0){
        perror("mutex init failed");
        exit(-1);
    }
    printf("SERVER RUNNING!!!\n");
    while(1){
        new_client=accept(sock,(struct sockaddr *)&client,(socklen_t *)&sockaddr_len);
        if (new_client<0){
            perror("Error accepting new connection");
            exit(-1);
        }
        printf("New client connected from port no %d and IP %s\n",ntohs(client.sin_port),inet_ntoa(client.sin_addr));
        args->new_client_sock=new_client;
        args->client_id=next_available_client;
        clients[next_available_client].client_ip=client.sin_addr.s_addr;
        clients[next_available_client].total_files=0;
        clients[next_available_client].next_available_file=0;
        next_available_client++;
        
        if ((pthread_create(&new_client_thread[next_available_client_thread],NULL,service_provider,args))!=0){
            perror("error creating service provider thread");
        }
        
        next_available_client_thread++;
    }
    printf("SERVER SHUTTING DOWN!!!\n");
    close(sock);
    return 0;
}