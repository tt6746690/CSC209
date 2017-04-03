#include <stdio.h>

#include "h_func.h"
#include "hash.h"


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
 * Returns
 * -- position of EOF in a char array buffer
 * -- -1 if not found
 */
int eof_pos(char *buffer){
    for(int i = 0; i < BUFSIZE; i++){
        if(buffer[i] == EOF){
            return i;
        }
    }
    return -1;
}


/*
 * precondition: req.st_mode yields regular file
 * Sends data specified by req by
 * -- open file at req.path
 * -- write to client socket where nbytes is
 * ---- BUFSIZE if eof is not reached
 * ---- position of EOF if eof is reached
 */
void send_data(int fd, struct request *req){

    FILE *f;
    if((f = fopen(req->path, "r")) == NULL){
        perror("client:open");
        exit(1);
    }

    int num_read, nbytes;
    char buffer[BUFSIZE];

    nbytes = BUFSIZE;

    while((num_read = fread(buffer, 1, BUFSIZE, f)) > 0){

        if(ferror(f) != 0){
            fprintf(stderr, "fread error: %s", req->path);
        }

        nbytes = (num_read == BUFSIZE) ? BUFSIZE : num_read;

        /* printf("buf = [%s] num_read = [%d] nbytes = [%d]\n", buffer, num_read, nbytes); */

        if(write(fd, buffer, nbytes) == -1){
            perror("client:write");
            exit(1);
        }
    }

    if(fclose(f) != 0){
        perror("client:fclose");
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
 * -- >0 the number of child processes created
 */
int traverse(const char *source, int sock_fd, char *host, unsigned short port){
    static int child_count = 0;

    // make & send request for source
    struct request client_req;
    make_req(source, &client_req);
    send_req(sock_fd, &client_req);

    // wait for response from server
    int res;
    int num_read = read(sock_fd, &res, sizeof(int));    //TODO: error check

    printf("%d \t%d \t%d \t%d \t%s\n",
            getpid(), sock_fd,
            client_req.type, res, client_req.path);


    if (res == SENDFILE){

        child_count++;

        int result = fork();
        if (result == 0){                // Child

            // Create a new socket for child process
            int child_sock_fd;
            child_sock_fd = client_sock(host, port);

            // Sending request
            int file_type = client_req.type;
            client_req.type = TRANSFILE;
            send_req(child_sock_fd, &client_req);

            /* Copy file / dir has two scenario
             * based on the type of file / dir
             * -- REGFILE
             * ---- client opens file and writes to client socket
             * ---- server reads and creates new file
             * -- REGDIR
             * ---- client just have to wait for OK
             * ---- server creates dir based on req alone
             */
            printf("\t\t\t\t\t\tc:%d \t%d \t%d \t%s \t",
                getpid(), client_req.size,
                client_req.mode, client_req.path);
            show_hash(client_req.hash);

            if(file_type == REGFILE){
                send_data(child_sock_fd, &client_req);
            }

            /*
             * File sender client receives 2 possible responses
             * OK
             * -- terminate child process with exit status of 0
             * ERROR
             * -- print appropriate msg with file causing error
             * -- and exit with status of 1
             */
            num_read = read(child_sock_fd, &res, sizeof(int));
            if(num_read == -1){
                perror("client:read");
                exit(1);
            }
            close(child_sock_fd);

            printf("%d \t%d \t%d \t%d \t%s\n",
                    getpid(), child_sock_fd,
                    client_req.type, res, client_req.path);

            if(res == OK){
                exit(0);
            } else if (res == ERROR){
                fprintf(stderr, "client: sock [%d] at [%s] receives "
                        "ERROR from server\n", child_sock_fd, source);
                exit(1);
            } else {
                printf("unexpected res = [%d]", res);
            }
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

    return child_count;

}

/*
 * The main client waits for count number of
 * child processes to terminate and report
 * based on exist status
 * 0 -- nothing
 * 1 -- error msg
 */
void client_wait(int count){

    while(count-- != 0){
        pid_t pid;
        int status;
        if((pid = wait(&status)) == -1) {
            perror("client:wait");
            exit(1);
        } else {

            if(!WIFEXITED(status)){
                fprintf(stderr, "client:wait return no status\n");
            } else if(WEXITSTATUS(status) == 0){
                // TODO: remove this afterwards. here just for debugging..
                fprintf(stdout, "[%d] terminated "
                        "with [%d] (success)\n", pid, WEXITSTATUS(status));
            } else if(WEXITSTATUS(status) == 1){
                fprintf(stdout, "[%d] terminated "
                        "with [%d] (error)\n", pid, WEXITSTATUS(status));
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
 * Delete client in head linked list with given fd
 * Return 0 if found and -1 if not found
 */
int linkedlist_delete(struct client *head, int fd){

    struct client *curr_ptr;
    struct client *prev_ptr = NULL;

    for(curr_ptr = head->next; curr_ptr != NULL;
            prev_ptr = curr_ptr, curr_ptr = curr_ptr->next){

        if(curr_ptr->fd == fd){

            if(prev_ptr == NULL){
                head->next = curr_ptr->next;
            } else{
                prev_ptr->next = curr_ptr->next;
            }

            free(curr_ptr);
            return 0 ;
        }

    }
    return -1;

}


/*
 * Print linked list at head
 * Each node is presented as fd
 */
void linkedlist_print(struct client *head){
    printf("\t\t\t\t\t\t HEAD -> ");
    struct client *curr_ptr = head->next;
    while(curr_ptr != NULL){
        printf("%d -> ", curr_ptr->fd);
        curr_ptr = curr_ptr->next;
    }
    printf(" NULL\n");
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
 * -- fd if
 * ---- file transfer socket finish transfer file
 * ---- main socket finish traversing filepath
 * -- 0 to continue reading req (default behaviour)
 * -- -1 if sys call fails
 */
int read_req(struct client *cli){
    int num_read;

    struct request *req = &(cli->client_req);

    int state = cli->current_state;
    int fd = cli->fd;

    if(state == AWAITING_TYPE){
        num_read = read(fd, &(req->type), sizeof(int));
        if (num_read == -1){
            perror("server:read");
            return -1;
        }
        cli->current_state = AWAITING_PATH;
    } else if(state == AWAITING_PATH){
        num_read = read(fd, req->path, MAXPATH);
        if (num_read == -1){
            perror("server:read");
            return -1;
        }
        cli->current_state = AWAITING_PERM;
    } else if(state == AWAITING_PERM){

        num_read = read(fd, &(req->mode), sizeof(mode_t));
        if (num_read == -1){
            perror("server:read");
            return -1;
        }
        cli->current_state = AWAITING_HASH;
    } else if(state == AWAITING_HASH){

        num_read = read(fd, req->hash, BLOCKSIZE);
        if (num_read == -1){
            perror("server:read");
            return -1;
        }
        cli->current_state = AWAITING_SIZE;
    } else if(state == AWAITING_SIZE){

        num_read = read(fd, &(req->size), sizeof(size_t));
        if (num_read == -1){
            perror("server:read");
            return -1;
        }
        /*
         * If request type is
         * TRANSFILE
         * -- Advance current_state to AWAITING_DATA
         * -- if transfering directory, we create dir with req
         * REGFILE or REGDIR
         * -- Send proper response signal
         * -- resets current_state to beginning to accept the next request
         */
        if(req->type == TRANSFILE){
            if(S_ISDIR(req->mode)){
                printf("sock = [%d] is copying dir [%s]\n", fd, req->path);
                return make_dir(cli);
            }
            cli->current_state = AWAITING_DATA;
        } else{
            compare_file(cli); // TODO return type necessary?
            cli->current_state = AWAITING_TYPE;
        }

    // Only type=TRANSFILE and copy file (not dir) reach here
    } else if(state == AWAITING_DATA){

        /* Copy file / dir has two scenario
         * based on the type of file / dir
         * -- REGFILE
         * ---- client opens file and writes to client socket
         * ---- server reads and creates new file
         * -- REGDIR
         * ---- client just have to wait for OK
         * ---- server creates dir based on req alone
         */

        if(S_ISREG(req->mode)){
            printf("sock = [%d] is copying file [%s]\n", fd, req->path);
            return make_file(cli);
        } else if(S_ISDIR(req->mode)) {
            printf("should not get here!\n");
        }

        return 0;
    }

    return 0;
}

/*
 * Compare files based on client request (cli->client_req)
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
int compare_file(struct client *cli){

    int response = 0;

    struct request req = cli->client_req;
    int client_fd = cli->fd;

    printf("%s\n", (cli->client_req).path);

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
    printf("%d \tres={%d} \t%d\n", client_fd, response, cli->current_state);

    return 0;
}


/*
 * Makes directory given client request with given
 * -- path
 * -- permission
 * Return -1 on error and fd if success
 */
int make_dir(struct client *cli){

    struct request *req = &(cli->client_req);
    int fd = cli->fd;

    int perm = req->mode & (S_IRWXU | S_IRWXG | S_IRWXO);

    if(mkdir(req->path, perm) == -1) {
        perror("mkdir");
        return -1;
    }

    int num_wrote, response;
    response = OK;
    num_wrote = write(fd, &response, sizeof(int));

    printf("[%s] (dir) copy finished\n", req->path);
    return fd;
}


/*
 * Makes file given client request with given
 * -- path
 * -- permission
 * Return
 * -- -1 on error
 * -- 0 if file copy not finished
 * -- fd if file copy finished
 * (i.e. file transfer over multiple select calls)
 */
int make_file(struct client *cli){

    struct request *req = &(cli->client_req);
    int fd = cli->fd;

    int perm = req->mode & (S_IRWXU | S_IRWXG | S_IRWXO);

    int nbytes = BUFSIZE;
    int num_read;
    int num_wrote;

    // Open file for write, create file if not exist
    FILE *dest_f;
    if((dest_f = fopen(req->path, "a+")) == NULL) {
        perror("server:fopen");
        return -1;
    }

    char buf[BUFSIZE];
    num_read = read(fd, buf, nbytes);

    printf("read %d bytes into buffer = [%s]\n", num_read, buf);

    if(num_read == -1) {
        perror("server:read");
        return -1;
    } else if(num_read != BUFSIZE){
        nbytes = num_read;
    }

    num_wrote = fwrite(buf, 1, nbytes, dest_f);
    if(num_wrote != nbytes){
        if(ferror(dest_f)){
            fprintf(stderr, "server:fwrite error at [%s]\n", req->path);
            return -1;
        }
    }

    // set permission
    if(chmod(req->path, perm) == -1){
        fprintf(stderr, "chmod: cannot set permission for [%s]\n", req->path);
    }


    // close FILE *
    if(fclose(dest_f) != 0){
        perror("server:fclose");
        return -1;
    }

    // copy is finished if read
    // -- is successful
    // -- number of bytes read is not BUFSIZE
    if(nbytes != BUFSIZE){
        int response = OK;
        num_wrote = write(fd, &response, sizeof(int));
        printf("[%s] (file) copy finished\n", req->path);
        return fd;
    }

    return 0;
}
