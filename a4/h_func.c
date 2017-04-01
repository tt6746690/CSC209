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
 */
void traverse(const char *source, int sock_fd, char *host, unsigned short port){

    // make & send request for source 
    struct request client_req;
    make_req(source, &client_req);
    send_req(sock_fd, &client_req);

    printf("Client sock=[%d] request: "
            "type=[%d], path=[%s] \n", 
            sock_fd, 
            client_req.type, client_req.path);

    // wait for response from server
    int res;
    int num_read = read(sock_fd, &res, sizeof(int));    //TODO: error check
    /* printf("Server response: [%d]\n", res); */

    if (res == SENDFILE){
        int result = fork();
        if (result == 0){                // Child

            int child_sock_fd;
            child_sock_fd = client_sock(host, port);

            // Send same request, with TRANSFILE type instead 
            client_req.type = TRANSFILE;

            printf("Client sock=[%d] request: "
                    "type=[%d], path=[%s] \t TRANSFILE\n", 
                    child_sock_fd, 
                    client_req.type, client_req.path);

            send_req(child_sock_fd, &client_req);

            // TODO: then send in file without expecting a message 
            // then wait for OK message  

            exit(1);
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

    return;
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
 * Reads request struct from client socket 
 */
int handle_cli(int server_fd, int client_fd, struct client *ct){
    struct request *client_req = &(ct->client_req);
    int num_read;

    // About to receive data in order: type, path, mode, hash, size
    // STEP 1: receive type of request from the server
    if (ct->current_state == AWAITING_TYPE){
        num_read = read(client_fd, &(client_req->type), sizeof(int));
        if (num_read == 0){
            return -1;
        }
        ct->current_state = AWAITING_PATH;

        // STEP 2: receive the path of the file
    } else if (ct->current_state == AWAITING_PATH){
        num_read = read(client_fd, client_req->path, MAXPATH);
        if (num_read == 0){
            return -1;
        }
        ct->current_state = AWAITING_PERM;

        // STEP 3: Mode
    } else if (ct->current_state == AWAITING_PERM){
        num_read = read(client_fd, &(client_req->mode), sizeof(mode_t));
        if (num_read == 0){
            return -1;
        }
        ct->current_state = AWAITING_HASH;

        // STEP 4: Hash
    } else if (ct->current_state == AWAITING_HASH){
        num_read = read(client_fd, client_req->hash, BLOCKSIZE);
        if (num_read == 0){
            return -1;
        }
        ct->current_state = AWAITING_SIZE;

        // Step 5: SIZE
    } else if (ct->current_state == AWAITING_SIZE){
        num_read = read(client_fd, &(client_req->size), sizeof(size_t));
        if (num_read == 0){
            return -1;
        }
        // Done reading all information for the request. Need to decide what to
        //  do next.
        if (client_req->type == REGFILE){
            /*printf("3. Type: %d at %p\n", client_req->type, &(client_req->type));
              printf("path: %s at %p\n", client_req->path, &(client_req->path));
              printf("mode: %d at %p\n", client_req->mode, &(client_req->mode));
              printf("hash: %s at %p\n", client_req->hash, &(client_req->hash));
              printf("size: %d at %p\n\n", client_req->size, &(client_req->size));*/
            file_compare(client_fd, client_req);
        } else if (client_req->type == REGDIR){
            /*printf("3. Type: %d at %p\n", client_req->type, &(client_req->type));
              printf("path: %s at %p\n", client_req->path, &(client_req->path));
              printf("mode: %d at %p\n", client_req->mode, &(client_req->mode));
              printf("hash: %s at %p\n", client_req->hash, &(client_req->hash));
              printf("size: %d at %p\n\n", client_req->size, &(client_req->size));*/
            file_compare(client_fd, client_req);
        } else {  // client_req->type == TRANSFILE
            /*printf("3. Type: %d at %p\n", client_req.type, &(client_req.type));
              printf("path: %s at %p\n", client_req.path, &(client_req.path));
              printf("mode: %d at %p\n", client_req.mode, &(client_req.mode));
              printf("hash: %s at %p\n", client_req.hash, &(client_req.hash));
              printf("size: %d at %p\n\n", client_req.size, &(client_req.size));*/
            ct->current_state = AWAITING_DATA;
        }

    } else if (ct->current_state == AWAITING_DATA){
        copy_file();
        // TODO: send OK after all transmission 
        // may propagate ERROR message to client 
        // print error with appropriate name 
        // remove ct from the linked list 
    }
    //printf("%s\n", "Successful information transfer");

    /*printf("2. Type: %d at %p\n", client_req->type, &(client_req->type));
      printf("path: %s at %p\n", client_req->path, &(client_req->path));
      printf("mode: %d at %p\n", client_req->mode, &(client_req->mode));
      printf("hash: %s at %p\n", client_req->hash, &(client_req->hash));
      printf("size: %d at %p\n", client_req->size, &(client_req->size));*/


    return client_fd;
}
/*
 * Take a struct req (of type REGFILE/REGDIR) and determines whether a copy
 * should be made (and tells the client).
 */
int file_compare(int client_fd, struct request *req){
    //struct stat server_file_stat;
    struct request new_request = *req;
    FILE *server_file = fopen(new_request.path, "r");
    if (server_file == NULL && errno != ENOENT){
        perror("fopen");
        exit(1);
    }
    int compare = 0;
    if (server_file != NULL){
        char file_hash[BLOCKSIZE];
        printf("%s exist on server\n", new_request.path);
        if (chmod(new_request.path, new_request.mode) == -1){
            perror("chmod");
            exit(1);
        }
        hash(file_hash, server_file);
        compare = check_hash(new_request.hash, file_hash);
        //show_hash(new_request.hash);
        //show_hash(file_hash);
    }
    int response = 0;
    if (compare || server_file == NULL){
        printf("Gotta copy %s\n", new_request.path);
        printf("\tcomp = %d\n", compare);
        if (server_file == NULL){
            printf("\t NULL!");
        }
        response = SENDFILE;
    } else{
        response = OK;
    }
    write(client_fd, &response, sizeof(int));
    return 0;
}

int copy_file(){

    return 0;

}
