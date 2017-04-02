#include <stdio.h>
#include "h_func.h"


/* Create a new socket that connects to host 
 * Waiting for a successful connection
 * Returns sock_fd and exits should error arises
 */
int client_sock(char *host, unsigned short port){

    int sock_fd;
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("client: socket");
        exit(1);
    }

    // Set the IP and port of the server to connect to.
    struct sockaddr_in server;
    server.sin_family = PF_INET;
    server.sin_port = htons(port);

    struct hostent *hp = gethostbyname(host);
    if ( hp == NULL ) {
        fprintf(stderr, "client: %s unknown host\n", host);
        exit(1);
    }
    server.sin_addr = *((struct in_addr *)hp->h_addr);

    // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("client:connect"); close(sock_fd);
        exit(1);
    }

    return sock_fd;
}
/* Construct client request for file/dir at path
 * request is modified to accomodate changes 
 * exits process on error 
 */
void make_req(const char *path, struct request *req){

    struct stat file_buf;
    if (lstat(path, &file_buf)){
        perror("client:lstat");
        exit(1);
    }
    strncpy(req->path, path, strlen(path) + 1);
    // Compute file hash
    FILE *f = fopen(req->path,"r");
    if (f == NULL){
        perror("client:fopen");
        exit(1);
    }
    // Populate client_req
    hash(req->hash, f);
    req->mode = file_buf.st_mode;
    req->size = file_buf.st_size;

    if (S_ISDIR(file_buf.st_mode)){
        req->type = REGDIR;
    } else{
        req->type = REGFILE;
    }
}

/*
 * Sends request struct to sock_fd over 5 read calls
 * In order of 
 * -- type 
 * -- path 
 * -- mode 
 * -- hash 
 * -- size
 */
void send_req(int sock_fd, struct request *req){
    if(write(sock_fd, &(req->type), sizeof(int)) == -1) {
        perror("client:write");
        exit(1);
    }
    if(write(sock_fd, req->path, MAXPATH) == -1) {
        perror("client:write");
        exit(1);
    }
    if(write(sock_fd, &(req->mode), sizeof(mode_t)) == -1) {
        perror("client:write");
        exit(1);
    }
    if(write(sock_fd, req->hash, BLOCKSIZE) == -1) {
        perror("client:write");
        exit(1);
    }
    if(write(sock_fd, &(req->size), sizeof(size_t)) == -1) {
        perror("client:write");
        exit(1);
    }
}


/*
 * Traverses filepath rooted at source with sock_fd
 * Then for each file or directory 
 * -- makes and sends request struct 
 * -- waits for response from server
 * ---- OK: continue to next file 
 * ---- SENDFILE: 
 * ------ forks new process, main process continues to next file, child:
 * ------ initiate new connection with server w/e request.type = TRANSFILE
 * ------ makes and sends request struct  
 * ------ transmit data from file 
 * ------ waits for OK, close socket and exits, otherwise handles error 
 * ----ERROR: print appropriate msg includes file name then exit(1) 
 * Return 
 * -- -1 for any error 
 * -- 0 for success
 */
void traverse(const char *source, int sock_fd, char *host, unsigned short port){

    // make & send request for source 
    struct request client_req;
    make_req(source, &client_req);
    send_req(sock_fd, &client_req);

    printf("%d \t%d \t%d \t%s \n", 
            getpid(), sock_fd, 
            client_req.type, client_req.path);


    // wait for response from server
    int res;
    int num_read = read(sock_fd, &res, sizeof(int));    //TODO: error check
    printf("Server response: [%d]\n", res);

    if (res == SENDFILE){
        int result = fork();
        if (result == 0){                // Child

            int child_sock_fd;
            child_sock_fd = client_sock(host, port);
            client_req.type = TRANSFILE;
            send_req(child_sock_fd, &client_req);
            
            printf("%d \t%d \t%d \t%s \n", 
                    getpid(), child_sock_fd, 
                    client_req.type, client_req.path);

            // TODO: Sending a file at source here... 

            close(child_sock_fd);
            /*
             * File sender client receives 2 possible responses
             * OK
             * -- terminate child process with exit status of 0
             * ERROR
             * -- print appropriate msg with file causing error 
             * -- and exit with status of 1
             */
            num_read = read(sock_fd, &res, sizeof(int));
            if(res == OK){
                exit(0);
            } else if (res == ERROR){
                fprintf(stderr, "client: sock [%d] at [%s] receives "
                        "ERROR from server\n", child_sock_fd, source);
                exit(1);
            }
            printf("sendfile again?");

        } else if (result < 0){
            perror("fork");
            exit(1);
        }

    } else if(res == ERROR){
        fprintf(stderr, "client: sock [%d] at [%s] receives "
                "ERROR from server\n", sock_fd, source);
        exit(1);
    }


    // tree traversal 
    struct stat file_buf;
    if (lstat(source, &file_buf)){
        perror("client:lstat");
        exit(1);
    }

    // recursively call traverse() if source is a directory
    if (S_ISDIR(file_buf.st_mode)){              
        DIR *dirp; 
        struct dirent *dp;

        if ((dirp = opendir(source)) == NULL){
            perror("opendir");
            exit(1);
        }

        while ((dp = readdir(dirp)) != NULL){     // traverse dirp
            if ((dp->d_name)[0] != '.'){          // avoid dot files

                // Compute "source/filename"
                char src_path[MAXPATH];
                strncpy(src_path, source, sizeof(src_path) - strlen(source) - 1);
                strncat(src_path, "/", sizeof(src_path) - strlen("/") - 1);
                strncat(src_path, dp->d_name, sizeof(src_path) - strlen(dp->d_name) - 1);

                traverse(src_path, sock_fd, host, port);
            }
        }
    } 
    

}


/*
 * Creates server socket 
 * binds to PORT and starts litening to 
 * connection from INADDR_ANY 
 */
int server_sock(unsigned short port){
    int sock_fd;
    int on = 1, status; 

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }

    // Configure option to use same port 
    status = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
            (const char *) &on, sizeof(on));
    if(status == -1) {
        perror("setsockopt -- REUSEADDR");
    }

    // Set up server address
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(&server.sin_zero, 0, 8);


    // Bind the selected port to the socket.
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }

    // Starts listening to connections
    if (listen(sock_fd, MAXCONNECTIONS) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }

    return sock_fd;
}

/*
 * Allocates memory for a new struct client 
 * at end of linked list with given fd 
 * Returns pointer to the newly created element 
 */
struct client *linkedlist_insert(struct client *head, int fd){

    /* end is the last element in linklist head */
    struct client *end;
    end = head;

    while(end->next != NULL){
        end = end->next;
    }

    /* allocates memory for a new client struct 
     * and insert till end of linked list */ 
    struct client *new_client;
    if((new_client = malloc(sizeof(struct client))) == NULL) {
        perror("server:malloc");
        exit(1);
    }

    end->next = new_client;

    // default values for client
    new_client->fd = fd;
    (new_client->client_req).type = AWAITING_TYPE;
    
    return new_client;
}


/*
 * Reads request struct from client to cli over 5 write calls
 * In order of 
 * -- type 
 * -- path 
 * -- mode 
 * -- hash 
 * -- size
 * Returns 
 * -- cli->fd if success
 * -- -1 if read failed
 */
int read_req(struct client *cli){
    int num_read;
    
    struct request *req = &(cli->client_req);
    int fd = cli->fd;

    switch (cli->current_state){
        case AWAITING_TYPE:
            num_read = read(fd, &(req->type), sizeof(int));
            if (num_read == -1){
                perror("server:read");
                return -1;
            }
            cli->current_state = AWAITING_PATH;
        case AWAITING_PATH: 
            num_read = read(fd, req->path, MAXPATH);
            if (num_read == -1){
                perror("server:read");
                return -1;
            }
            cli->current_state = AWAITING_PERM;
        case AWAITING_PERM:
            num_read = read(fd, &(req->mode), sizeof(mode_t));
            if (num_read == -1){
                perror("server:read");
                return -1;
            }
            cli->current_state = AWAITING_HASH;
        case AWAITING_HASH: 
            num_read = read(fd, req->hash, BLOCKSIZE);
            if (num_read == -1){
                perror("server:read");
                return -1;
            }
            cli->current_state = AWAITING_SIZE;
        case AWAITING_SIZE:
            num_read = read(fd, &(req->size), sizeof(size_t));
            if (num_read == -1){
                perror("server:read");
                return -1;
            }

            if(req->type == TRANSFILE){
                cli->current_state = AWAITING_DATA;
                return fd;
            } 

            /*
             * If request type is either REGFILE or REGDIR
             * Send proper response signal and resets 
             * current_state to beginning to accept the next request
             */
            send_res(cli);
            cli->current_state = AWAITING_TYPE;

        case AWAITING_DATA: 
            // sending OK for now 
            write(fd, OK, sizeof(int));

            // TODO: send OK after all transmission 
            // may propagate ERROR message to client 
            // print error with appropriate name 
            // remove ct from the linked list 

    }

    return fd;
}

/*
 * Based on client request (cli->client_req)
 * Sends res signal to client
 * SENDFILE
 * -- server_file does not exist 
 * -- server_file different in hash from client_file 
 * OK
 * -- server_file and client_file are identical 
 * ERROR 
 * -- file types are incompatible (i.e. file vs. directory)
 * Return 
 */
int send_res(struct client *cli){

    int response = 0;

    struct request req = cli->client_req;
    int client_fd = cli->fd;

    // Check if file exists on server
    FILE *server_file = fopen(req.path, "r");
    if (server_file == NULL && errno != ENOENT){
        perror("fopen");
        return -1;
    }

    // Compare hash if file does exists 
    int compare = 0;
    if (server_file != NULL){
        char file_hash[BLOCKSIZE];
        printf("%s exist on server\n", req.path);
        if (chmod(req.path, req.mode) == -1){
            perror("chmod");
            exit(1);
        }
        hash(file_hash, server_file);
        compare = check_hash(req.hash, file_hash);
        //show_hash(new_request.hash);
        //show_hash(file_hash);
    }

    // Sends appropriate response 
    if (compare || server_file == NULL){
        response = SENDFILE;
    } else{
        response = OK;
    }

    write(client_fd, &response, sizeof(int));
    printf("server: sent response = [%d]\n", response);
    
    return 0;
}

