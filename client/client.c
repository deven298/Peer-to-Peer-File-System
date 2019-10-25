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
#define MAX_BUFFER_SIZE 512    //setting maximum size of buffer for sending and receiving data
#define MAX_CLIENTS 8
#define MAX_FILES   30
#define TOTAL_CHUNKS 8

//server info
#define SERVER_IP "SERVER IP ADDRESS HERE"   //I am only allowing localhost to act as client and server
#define MY_IP "YOUR IP ADDRESS HERE"   //clients IP
#define SERVER_LISTENING_PORT "SERVER LISTENING PORT HERE"

struct file_info
{
    char file_name[MAX_BUFFER_SIZE];    //file name
    int file_size;  //file size
    int file_chunk[TOTAL_CHUNKS]; //each file will have 8 chunks and 0 means chunk not present and 1 means chunk present
};

struct client_info
{
    uint32_t client_ip; //client ip
    uint16_t client_listening_port;   //client port
    int total_files;    //total number of files
    int next_available_file;    //index of files this client has
};

struct client_info clients[MAX_CLIENTS];    //array of clients in the network
int next_available_client;    //index of next available client
struct file_info clients_file_info[MAX_CLIENTS][MAX_FILES]; //array of files of each client in the network
int server_client_id;   //this clients id in server

//main server
int sock;   //socket descriptor for connecting to server
struct sockaddr_in remote_server;  //contains IP and port number of server
char file_dn[MAX_BUFFER_SIZE];  //name of the file requested for download
pthread_mutex_t lock;

//handle multiple download reqs
pthread_t handle_down_thread[MAX_CLIENTS];
int next_available_down_thread;

struct handle_down_info{
    int client_sock;
};

//print functions
void print_metadata(){
    printf("next_available client: %d\n",next_available_client);
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
    len=send(sock,(void *)&next_available_client,sizeof(int),0);    //send next available client integer
    len=send(sock,(void *)clients,sizeof(clients),0);   //send clients
    curr_len=len;
    while (curr_len!=sizeof(clients)){
        len=send(sock,(void *)clients+curr_len,sizeof(clients)-curr_len,0);
        curr_len=curr_len+len;
    }
    len=send(sock,(void *)clients_file_info,sizeof(clients_file_info),0);   //send clients file info
    curr_len=len;
    while (curr_len!=sizeof(clients_file_info)){
        len=send(sock,(void *)clients_file_info,sizeof(clients_file_info)-curr_len,0);
        curr_len+=len;
    }
    pthread_mutex_unlock(&lock);
    // print_metadata();
}

void rec_metadata(int sock){
    int curr_len;
    int len;
    pthread_mutex_lock(&lock);
    int temp=0;
    len=read(sock,(void *)&temp,sizeof(temp));    //recv next available client integer
    next_available_client=ntohl(temp);
    
    len=read(sock,(void *)clients,sizeof(clients));   //recv clients
    curr_len=len;
    while (curr_len!=sizeof(clients)){
        len=read(sock,(void *)clients+curr_len,sizeof(clients)-curr_len);
        curr_len+=len;
    }
    
    len=read(sock,(void *)clients_file_info,sizeof(clients_file_info));   //recv clients_file_info
    curr_len=len;
    while (curr_len!=sizeof(clients_file_info)){
        len=read(sock,(void *)clients_file_info+curr_len,sizeof(clients_file_info)-curr_len);
        curr_len+=len;
    }
    pthread_mutex_unlock(&lock);
    // print_metadata();
}

char *read_from_file(char* filename,int filesize,int chunknum){
    int chunksize;
    if (chunknum==TOTAL_CHUNKS-1){
        chunksize=filesize-((filesize/TOTAL_CHUNKS)*(TOTAL_CHUNKS-2));
    } else {
        chunksize=filesize/TOTAL_CHUNKS;
    }
    char *senddata=(char *)malloc(chunksize);
    FILE *file_req=fopen(filename,"r");
    int start_pos=(filesize/TOTAL_CHUNKS)*chunknum;
    fseek(file_req,start_pos,SEEK_CUR);
    if (file_req==NULL){
        fprintf(stderr,"ERROR: Requested file not found \n");
    } else {
        bzero(senddata,chunksize);
        int bytes_rec=fread(senddata,sizeof(char),chunksize,file_req);
        fclose(file_req);
    }
    return senddata;
}

void *handle_download(void *arg){
    struct handle_down_info *temp_sock=(struct handle_down_info *)arg;
    int new_client_sock=temp_sock->client_sock;
    char senddata[MAX_BUFFER_SIZE];
    char recdata[MAX_BUFFER_SIZE];
    int len=0;
    while (1){
        bzero(recdata,MAX_BUFFER_SIZE);
        len=recv(new_client_sock,recdata,MAX_BUFFER_SIZE,0);    //rec filename
        if (len==0){
            // printf("\nPeer has exited\n");
            break;
        } else {
            // printf("%s\n", recdata); //file name of file requested by other client
            char *req_file=recdata;
            int filesize;
            len=recv(new_client_sock,&filesize,sizeof(filesize),0); //rec filesize
            int chunknum;
            len=recv(new_client_sock,&chunknum,sizeof(chunknum),0); //rec chunknum
            int chunk_size;
            if (chunknum==TOTAL_CHUNKS-1){
                chunk_size=filesize-((filesize/TOTAL_CHUNKS)*(TOTAL_CHUNKS-1));
            } else {
                chunk_size=filesize/TOTAL_CHUNKS;
            }
            char *read_buffer=read_from_file(req_file,filesize,chunknum);
            int curr_len;
            len=send(new_client_sock,read_buffer,chunk_size,0);
            curr_len=len;
            while(curr_len!=chunk_size){
                len=send(new_client_sock,read_buffer+curr_len,chunk_size-curr_len,0);
                curr_len=curr_len+len;
            }
            // free(read_buffer);
            bzero(recdata,MAX_BUFFER_SIZE);
        }
    }
    
    close(new_client_sock);
    return NULL;
}

void *client_server(void *l_port){
    uint16_t *temp_port=(uint16_t *)l_port;
    uint16_t listening_port=*temp_port;

    int listen_sock;    //socket descriptor for listening to incoming connections
    struct sockaddr_in server; //server structure when client acting as server
    struct sockaddr_in client; //structure for client acting as server to bind to particular incoming client
    int sockaddr_len=sizeof(struct sockaddr_in);

    int new_client_sock;
    pthread_t handle_down_thread[MAX_CLIENTS];
    int next_handle_down_thread=0;
    

    if ((listen_sock=socket(AF_INET,SOCK_STREAM,0))<0){
        perror("listening socket: ");
        exit(-1);
    }

    //client as server
    server.sin_family=AF_INET;
    server.sin_port=listening_port;
    server.sin_addr.s_addr=inet_addr(MY_IP);
    bzero(&server.sin_zero,8);

    if ((bind(listen_sock,(struct sockaddr *)&server,sockaddr_len))==ERROR){
        perror("listening bind: ");
        exit(-1);
    }
    
    if (listen(listen_sock,64)==ERROR){
        perror("listen");
        exit(-1);
    }

    while(1){
        new_client_sock=accept(listen_sock,(struct sockaddr *)&client,(socklen_t *)&sockaddr_len);
        if (new_client_sock==ERROR){
            perror("error accepting in client");
            exit(-1);
        } 
        // else {
        //     printf("\nNew peer connected from port no %d and IP %s\n",ntohs(client.sin_port),inet_ntoa(client.sin_addr));
        // }
        //for parallel downloading
        struct handle_down_info *handle_down_arg=(struct handle_down_info *)malloc(sizeof(struct handle_down_info));
        handle_down_arg->client_sock=new_client_sock;
        if ((pthread_create(&handle_down_thread[next_handle_down_thread],NULL,handle_download,(void *)handle_down_arg))!=0){
            perror("handle download thread error");
            exit(-1);
        }
        next_handle_down_thread++;
        new_client_sock=-1;
    }
    printf("we should not be here\n");
    close(listen_sock);
    return NULL;
}

int get_rarest(char *filename,int *rarest_client_ids,int *dn_chunk_num,int *chunk_rec){
    int filesize=0;
    int chunk_mat[TOTAL_CHUNKS][MAX_CLIENTS];
    for (int i=0;i<TOTAL_CHUNKS;i++){
        for (int j=0;j<MAX_CLIENTS;j++){
            chunk_mat[i][j]=0;
        }
    }

    for (int i=0;i<next_available_client;i++){
        for (int j=0;j<clients[i].next_available_file;j++){
            if (!strcmp(clients_file_info[i][j].file_name,filename)){
                filesize=clients_file_info[i][j].file_size;
                for (int k=0;k<TOTAL_CHUNKS;k++){
                    if(clients_file_info[i][j].file_chunk[k]){
                        chunk_mat[k][i]=1;
                    }
                }
            }
        }
    }
    //dont consider client where the download is happening
    for (int k=0;k<TOTAL_CHUNKS;k++){
        chunk_mat[k][server_client_id]=0;
    }
    //dont consider already downloaded chunks
    for (int k=0;k<TOTAL_CHUNKS;k++){
        if (chunk_rec[k]){
            for (int i=0;i<MAX_CLIENTS;i++){
                chunk_mat[k][i]=0;
            }
        }
    }
    
    //find rarest
    int all_good=0;
    while (!all_good){
        int min_chunk=MAX_CLIENTS;
        int tk;
        for (int k=0;k<TOTAL_CHUNKS;k++){
            int sum_k=0;
            for (int i=0;i<MAX_CLIENTS;i++){
                sum_k=sum_k+chunk_mat[k][i];
            }
            if (sum_k<min_chunk && sum_k!=0){
                min_chunk=sum_k;
                tk=k;
            }
        }
        //select peer which has rarest chunk
        int ti;
        for (int i=0;i<MAX_CLIENTS;i++){
            if (chunk_mat[tk][i]){
                // printf("%d\n",i);
                ti=i;
                break;
            }
        }
        
        //remove peer for finding next rarest
        for (int k=0;k<TOTAL_CHUNKS;k++){
            chunk_mat[k][ti]=0;
        }
        //remove chunk chunk for finding next rarest
        for (int i=0;i<MAX_CLIENTS;i++){
            chunk_mat[tk][i]=0;
        }
        
        rarest_client_ids[ti]=1;
        dn_chunk_num[ti]=tk+1;
        
        int all_zero=1; //1 means true and 0 means false
        for (int k=0;k<TOTAL_CHUNKS;k++){
            for (int i=0;i<MAX_CLIENTS;i++){
                if (chunk_mat[k][i]==1){
                    all_zero=0;
                }
            }
        }
        if (all_zero){
            all_good=1;
        }
    }
    return filesize;
}

struct file_thread_args{
    char *filename;
    int file_size;
    int chunk_num;
    int socket_fd;
};

void *file_write(void *arg){
    struct file_thread_args *args=(struct file_thread_args *)arg;
    char *filename=args->filename;
    int filesize=args->file_size;
    int chunknum=args->chunk_num;
    int socketfd=args->socket_fd;
    int chunksize;
    if (chunknum==TOTAL_CHUNKS-1){
        chunksize=filesize-((filesize/TOTAL_CHUNKS)*(TOTAL_CHUNKS-1));
    } else {
        chunksize=filesize/TOTAL_CHUNKS;
    }
    char *recdata=(char *)malloc(chunksize);
    send(socketfd,filename,MAX_BUFFER_SIZE,0);  //send filename
    send(socketfd,&filesize,sizeof(filesize),0);    //send filesize
    send(socketfd,&chunknum,sizeof(chunknum),0);    //send chunknum
    FILE *wfile=fopen(filename,"a+");
    int start_pos=(filesize/TOTAL_CHUNKS)*chunknum;
    fseek(wfile,start_pos,SEEK_CUR);
    if (wfile==NULL){
        fprintf(stderr,"ERROR: Requested file not found \n");
        free(recdata);
        // free(args);
    } else {
        int len;
        int bytes_rec;
        int curr_len;
        len=recv(socketfd,recdata,chunksize,0);   //recv data to write
        curr_len=len;
        while (curr_len!=chunksize){
            len=recv(socketfd,recdata+curr_len,chunksize-curr_len,0);
            curr_len=curr_len+len;
        }
        bytes_rec=fwrite(recdata,sizeof(char),chunksize,wfile);
        bzero(recdata,chunksize);
        fclose(wfile);
        // free(recdata);
        // free(args);
    }
    return NULL;
}

void *client_download(void *fname){
    char *filename=(char *)fname;
    char *key;
    printf("DOWNLOADING FILE %s...\n",filename);
    int all_chunks_received=0;  //act as bolean to make sure we have received all chunks
    int chunks_rec[TOTAL_CHUNKS];
    //initialize chunk_rec
    for (int k=0;k<TOTAL_CHUNKS;k++){
        chunks_rec[k]=0;
    }
    //
    //file size
    for (int i=0;i<MAX_CLIENTS;i++){
        for (int j=0;j<clients[i].next_available_file;j++){
            if (!strcmp(clients_file_info[i][j].file_name,filename)){
                clients_file_info[server_client_id][clients[server_client_id].next_available_file].file_size=clients_file_info[i][j].file_size;
            }
        }
    }
    //
    strcpy(clients_file_info[server_client_id][clients[server_client_id].next_available_file].file_name,filename);
    for (int k=0;k<TOTAL_CHUNKS;k++){
        clients_file_info[server_client_id][clients[server_client_id].next_available_file].file_chunk[k]=chunks_rec[k];

    }
    clients[server_client_id].total_files++;
    clients[server_client_id].next_available_file++;
    send_metadata(sock);
    // print_metadata();
    //get rarest vaiables
    int rarest_client_ids[MAX_CLIENTS];
    int dn_chunk_num[MAX_CLIENTS];
    int client_sockets[MAX_CLIENTS];
    for (int i=0;i<MAX_CLIENTS;i++){
        client_sockets[i]=-1;
    }
    struct sockaddr_in client_sockets_info[MAX_CLIENTS];
    pthread_t client_dn_thread_fd[MAX_CLIENTS];
    char recdata[MAX_BUFFER_SIZE];
    int len;
    int filesize;

    //connect to clients who have the required file
    for (int i=0;i<next_available_client;i++){
        for (int j=0;j<clients[i].next_available_file;j++){
            if (!strcmp(clients_file_info[i][j].file_name,filename) && i!=server_client_id){
                //get socket descriptor
                client_sockets[i]=socket(AF_INET,SOCK_STREAM,0);
                if (client_sockets[i]<0){
                    perror("client_sockets socket error");
                    exit(-1);
                }
                client_sockets_info[i].sin_family=AF_INET;
                client_sockets_info[i].sin_port=clients[i].client_listening_port;
                client_sockets_info[i].sin_addr.s_addr=clients[i].client_ip;
                
                //connect to clients
                if ((connect(client_sockets[i],(struct sockaddr *)&client_sockets_info[i],sizeof(struct sockaddr_in)))==ERROR){
                    perror("connection failed");
                    exit(-1);
                }
                printf("Connected to client %d for downloading\n",i);
            }
        }
    }
    //
    
    while(!all_chunks_received){
        key="re";
        send(sock,(void *)key,MAX_BUFFER_SIZE,0);
        rec_metadata(sock);
        
        //connect to clients who have the required file
        for (int i=0;i<next_available_client;i++){
            for (int j=0;j<clients[i].next_available_file;j++){
                if ((client_sockets[i]==-1) && i!=server_client_id && (!strcmp(clients_file_info[i][j].file_name,filename))){
                    //connect with peer
                    //get socket descriptor
                    client_sockets[i]=socket(AF_INET,SOCK_STREAM,0);
                    if (client_sockets[i]<0){
                        perror("client_sockets socket error");
                        exit(-1);
                    }
                    client_sockets_info[i].sin_family=AF_INET;
                    client_sockets_info[i].sin_port=clients[i].client_listening_port;
                    client_sockets_info[i].sin_addr.s_addr=clients[i].client_ip;
                    // printf("client listening port: %u\n",clients[i].client_listening_port);
                    // printf("client IP addr: %u\n",clients[i].client_ip);
                    // print_metadata();
                    // printf("%d, %d\n",i,client_sockets[i]);
                    if ((connect(client_sockets[i],(struct sockaddr *)&client_sockets_info[i],sizeof(struct sockaddr_in)))==ERROR){
                        perror("connection failed");
                        exit(-1);
                    }
                    printf("Connected to client %d for downloading\n",i);
                }
            }
        }
        //
        
        //get rearest chunks
        filesize=get_rarest(filename,rarest_client_ids,dn_chunk_num,chunks_rec);    //returns file size of filename
        
        //parallel downloading from all peers
        for (int i=0;i<MAX_CLIENTS;i++){
            // printf("%d, %d\n",i,rarest_client_ids[i]);
            if(rarest_client_ids[i]){
                struct file_thread_args *file_fun_args=(struct file_thread_args *)malloc(sizeof(struct file_thread_args));
                file_fun_args->filename=filename;
                file_fun_args->file_size=filesize;
                file_fun_args->chunk_num=dn_chunk_num[i]-1;
                file_fun_args->socket_fd=client_sockets[i];
                printf("DOWNLOADING CHUNK %d FROM CLIENT %d\n",dn_chunk_num[i]-1,i);
                if ((pthread_create(&client_dn_thread_fd[i],NULL,file_write,file_fun_args))!=0){
                    perror("error downloading chunk from thread");
                }
            }
        }
        for(int i=0;i<MAX_CLIENTS;i++){
            if(rarest_client_ids[i]){
                if (pthread_join(client_dn_thread_fd[i],NULL)==0){
                    printf("CHUNK FROM CLIENT %d DOWNLOADED SUCCESSFULLY!!!\n",i);
                }
                chunks_rec[dn_chunk_num[i]-1]=1;
            }
        }
        //update required variables and check if all chunks received
        for (int i=0;i<MAX_CLIENTS;i++){
            rarest_client_ids[i]=0;
            dn_chunk_num[i]=0;
            client_dn_thread_fd[i]=0;
        }
        for (int k=0;k<TOTAL_CHUNKS;k++){
            all_chunks_received=chunks_rec[k];
        }
        
        for (int k=0;k<TOTAL_CHUNKS;k++){
            clients_file_info[server_client_id][clients[server_client_id].next_available_file-1].file_chunk[k]=chunks_rec[k];
        }
        key="ud";
        send(sock,(void *)key,MAX_BUFFER_SIZE,0);
        send_metadata(sock);
        
        printf("CHUNK REGISTRATION SUCCESSFUL!!!\n");
    }
    key="re";
    send(sock,(void *)key,MAX_BUFFER_SIZE,0);
    rec_metadata(sock);
    return NULL;
}

int main(int argc, char **argv){
    char senddata[MAX_BUFFER_SIZE]; //user input
    char recdata[MAX_BUFFER_SIZE];  //received data from server
    int len;    //length of received buffer

    //client takes three arguements SERVER IP ADDRESS,SERVER PORT WITH PEER LISTENING PORT
    if (argc < 1){
        fprintf(stderr,"ERROR, ENTER YOUR LISTENING PORT\n");
		exit(-1);
	}
    //for service
    int service_choice; //take user input of service
    char *key;  //unique key to send to server
    uint16_t *LISTENING_PORT=(uint16_t *)malloc(sizeof(uint16_t));
    *LISTENING_PORT=htons(atoi(argv[1]));
    // printf("listening port: %u\n",LISTENING_PORT);

    //variables for download operation
    pthread_t file_dn_thread;   //file download thread

    //variables for acting as server
    pthread_t server_thread;    //server thread

    //for connecting with server
	if ((sock=socket(AF_INET,SOCK_STREAM,0))==ERROR){ 
		perror("Connecting to socket: ");
		exit(-1);  
	}

    remote_server.sin_family=AF_INET;
    remote_server.sin_port=htons(SERVER_LISTENING_PORT);
    remote_server.sin_addr.s_addr=inet_addr(SERVER_IP);
    bzero(&remote_server.sin_zero,8);

    if ((connect(sock,(struct sockaddr *)&remote_server,sizeof(struct sockaddr_in)))==ERROR){
        perror("connection failed: ");
        exit(-1);
    }
    printf("%s","Connected to server\n");
    len=recv(sock,&server_client_id,sizeof(server_client_id),0);    //recv client id from server
    // printf("%d\n",server_client_id);
    send(sock,LISTENING_PORT,sizeof(uint16_t),0);    //send listening port

    if (pthread_mutex_init(&lock, NULL) != 0){
        perror("mutex init failed");
        exit(-1);
    }

    //server running in parallel
    if ((pthread_create(&server_thread,NULL,client_server,(void *)LISTENING_PORT)!=0)){
        perror("Server thread error");
        exit(-1);
    }

    while(1){
        //GUI
        printf("\nPLEASE ENTER YOUR CHOICE:\n");
        printf("1. UPLOAD FILE/S\n");
        printf("2. SEARCH FOR FILE/S\n");
        printf("3. DOWNLOAD FILE/S\n");
        printf("4. EXIT (YOU WILL NOT BE ABLE TO USE ANY OF THE SERVICES)\n");
        printf("Enter your choice: ");
        scanf("%d",&service_choice);
        if (service_choice<=0 || service_choice>4){
            printf("ENTER AN INTEGER FROM 1 TO 4...\n");
            exit(-1);
        } else {
            switch(service_choice){
                case 1: //UPLOAD FILE/S
                    key="up";   //key to be send to server so that it knows it is an upload request
                    send(sock,(void *)key,MAX_BUFFER_SIZE,0);
                    printf("ENTER THE FILE NAME (MULTIPLE FILES SPACE SEPARATED): ");
                    scanf(" %99[^\n]",senddata);
                    // printf("%s\n",senddata);
                    //splitting the files
                    int total_files=0;
                    char delim[]=" ";
                    char one_cpy[MAX_BUFFER_SIZE];
                    strcpy(one_cpy,senddata);
                    char *one_file = strtok(one_cpy, delim);
                    // printf("%s\n",one_file);
                    while (one_file!=NULL){
                        // printf("%s\n",one_file);
                        total_files++;
                        one_file=strtok(NULL,delim);
                    }
                    // printf("%d\n",total_files);
                    send(sock,(void *)&total_files,sizeof(total_files),0);  //send total files
                    //send file one by one
                    char two_cpy[MAX_BUFFER_SIZE];
                    strcpy(two_cpy,senddata);
                    char *two_file=strtok(two_cpy, delim);
                    while (two_file!=NULL){
                        FILE *tfp=fopen(two_file,"r");
                        if (tfp==NULL){
                            printf("file cant open\n");
                        } else {
                            char filename[MAX_BUFFER_SIZE];
                            strcpy(filename,two_file);
                            send(sock,(void *)filename,MAX_BUFFER_SIZE,0);  //send filename
                            fseek(tfp,0,SEEK_END);
                            int filesize=ftell(tfp);
                            send(sock,(void *)&filesize,sizeof(filesize),0);    //send filesize
                            int chunk[TOTAL_CHUNKS];
                            for (int k=0;k<TOTAL_CHUNKS;k++){
                                chunk[k]=1;
                            }
                            send(sock,(void *)chunk,sizeof(chunk),0);   //send chunk arr
                            // printf("%s, %d\n",two_file,filesize);
                            fclose(tfp);
                        }
                        // printf("b: %s\n",two_file);
                        two_file=strtok(NULL,delim);
                        // printf("a: %s\n",two_file);
                    }
                    len=recv(sock,(void *)recdata,MAX_BUFFER_SIZE,0);
                    recdata[len]='\0';
                    printf("%s\n",recdata); //display confirmation or failure message
                    bzero(recdata,MAX_BUFFER_SIZE);
                    // print_metadata();
                break;
                case 2:
                    key="se";   //key for search
                    send(sock,(void *)key,MAX_BUFFER_SIZE,0);

                    printf("ENTER THE FILE NAME TO SEARCH: ");
                    scanf(" %[^\t\n]s",senddata);
                    send(sock,(void *)senddata,strlen(senddata),0);
                    printf("SEARCHING FOR FILES...\n");
                    printf("FILE_NAME\tCHUNK_NO\tFILE_SIZE\tCLIENT_IP\tCLIENT_PORT\n");   //format of received search results
                    //rec everything from server
                    rec_metadata(sock);
                    // print_metadata();
                    int file_found=0;   //act as boolean to see if file was found
                    for (int i=0;i<next_available_client;i++){
                        for (int j=0;j<clients[i].next_available_file;j++){
                            if (!strcmp(clients_file_info[i][j].file_name,senddata)){
                                for (int k=0;k<TOTAL_CHUNKS;k++){
                                    if (clients_file_info[i][j].file_chunk[k]==1){
                                        file_found=1;
                                        printf("%s\t%d\t%d\t%lu\t%u\n",clients_file_info[i][j].file_name,k,clients_file_info[i][j].file_size,(unsigned long)clients[i].client_ip,(unsigned)clients[i].client_listening_port);
                                    }
                                }
                            }
                        }
                    }
                    if (!file_found){
                        printf("SORRY! THE FILE YOU ARE LOOKING FOR IS NOT IN THE NETWORK YET.\n");
                    }
                    printf("SEARCH COMPLETED!!!\n");
                    printf("CHOOSE OPTION 3 TO DOWNLOAD FILE/S\n");
                break;
                case 3: //download file/s
                    key="dn";
                    send(sock,(void *)key,MAX_BUFFER_SIZE,0);

                    printf("ENTER FILE TO BE DOWNLOADED: ");
                    scanf(" %[^\t\n]s",file_dn);
                    send(sock,file_dn,strlen(file_dn),0);
                    // printf("LOOKING FOR FILES IN NETWORK...\n");
                    
                    rec_metadata(sock);

                    if ((pthread_create(&file_dn_thread,NULL,client_download,(void *)file_dn))!=0){
                        perror("ERROR CREATING DOWNLOAD THREAD");
                    }
                    if (pthread_join(file_dn_thread,NULL)==0){
                        printf("DOWNLOAD SUCCESSFUL!!!\n");
                    }
                    // send_metadata(sock);
                break;
                case 4:
                    //EXIT (act as server only, can only share data after choosing this option)
                    key="ex"; //key for exit
                    send(sock,(void *)key,MAX_BUFFER_SIZE,0);
                    close(sock);
                    printf("YOU HAVE BEEN DISCONNECTED FROM THE SERVER!!!\n");
                    return 0;
                
                default:
                    printf("INVALID OPTION SELECTED.\n");
            }
        }
    }
    return 0;
}