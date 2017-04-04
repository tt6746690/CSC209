#include <stdio.h>

#include "server.h"

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

    /* Initialize new_client to default values */
    new_client->fd = fd;
    new_client->current_state = AWAITING_TYPE;
    new_client->file = NULL; 
    new_client->client_req = (const struct request) {0};
    new_client->next = NULL;

    return new_client;
}

/*
 * Delete client in head linked list with given fd
 * Return element before deleted item if found; NULL otherwise
 */
struct client *linkedlist_delete(struct client *head, int fd){

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
            return (prev_ptr == NULL) ? head : prev_ptr;
        }

    }
    return NULL;


}

/*
 * Print linked list at head
 * Each node is presented as fd
 */
void linkedlist_print(struct client *head){
    printf("HEAD -> ");
    struct client *curr_ptr = head->next;
    while(curr_ptr != NULL){
        int fd = curr_ptr->fd;
        int state = curr_ptr->current_state;

        struct request req = curr_ptr->client_req;
        int req_type = req.type;
        char *path = req.path;

        printf("%d [state=%d](type=%d path=%s) -> ",
                fd, state, req_type, path);

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

    if(state == AWAITING_TYPE){             // 0
        num_read = read(fd, &(req->type), sizeof(int));
        if (num_read == -1){
            perror("server:read");
            return -1;
        } else if (num_read == 0){    // close fd if client conenction closed
            return fd;
        }
        cli->current_state = AWAITING_PATH;
    } else if(state == AWAITING_PATH){      // 1
        num_read = read(fd, req->path, MAXPATH);
        if (num_read == -1){
            perror("server:read");
            return -1;
        } else if(num_read == 0){
            return -1;
        }
        cli->current_state = AWAITING_PERM;
    } else if(state == AWAITING_PERM){      // 3

        num_read = read(fd, &(req->mode), sizeof(mode_t));
        if (num_read == -1){
            perror("server:read");
            return -1;
        } else if(num_read == 0){
            return -1;
        }
        if (num_read == 0) return -1;
        cli->current_state = AWAITING_HASH;
    } else if(state == AWAITING_HASH){      // 4

        num_read = read(fd, req->hash, BLOCKSIZE);
        if (num_read == -1){
            perror("server:read");
            return -1;
        } else if(num_read == 0){
            return -1;
        }
        cli->current_state = AWAITING_SIZE;
    } else if(state == AWAITING_SIZE){      // 2

        num_read = read(fd, &(req->size), sizeof(size_t));
        if (num_read == -1){
            perror("server:read");
            return -1;
        } else if(num_read == 0){
            return -1;
        }
        /*
         * If request type is
         * TRANSFILE
         * -- if transfering directory, we create dir with req
         * -- Otherwise advance current_state to AWAITING_DATA and continue
         * REGFILE or REGDIR
         * -- Compare request and destination, respond with proper response
         * -- resets current_state to beginning to accept the next request
         */
        if(req->type == TRANSFILE){
            if(S_ISDIR(req->mode)){
                return make_dir(cli);
            }
            cli->current_state = AWAITING_DATA;
        } else{
            compare_file(cli); // TODO return type necessary? yes 
            cli->current_state = AWAITING_TYPE;
        }
    } else if(state == AWAITING_DATA){          // 5

        /* Only file is copied in this state
         * Precondition
         * -- req.type = TRANSFILE && req.path holds non-directory
         * steps
         * -- open file stream and store to client struct 
         * -- subsequently read over multiple connections
         * ---- if necessary to copy over files
         */
        if(S_ISREG(req->mode)){
            if (cli->file == NULL)
                return make_file(cli);
            else
                return write_file(cli);
        } else if(S_ISDIR(req->mode)) {
            // Only type=TRANSFILE and copy file (not dir) reach here
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

    //printf("%s\n", (cli->client_req).path);

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
        //printf("%s exist on server\n", req.path);
        if (chmod(req.path, req.mode) == -1){
            perror("chmod");
            exit(1);
        }
        hash(file_hash, server_file);
        compare = check_hash(req.hash, file_hash);
        //show_hash(req.hash);
        //show_hash(file_hash);
    }

    // Sends appropriate response
    if (compare || server_file == NULL){
        response = SENDFILE;
    } else{
        response = OK;
    }
    write(client_fd, &response, sizeof(int));
    //printf("%d \tres={%d} \t%d\n", client_fd, response, cli->current_state);

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

    printf("\t\t\t\t\t[%s] (dir) copy finished\n", req->path);
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

    int perm = req->mode & (S_IRWXU | S_IRWXG | S_IRWXO);

    // Open file for write, create file if not exist
    //FILE *dest_f;
    if((cli->file = fopen(req->path, "w+")) == NULL) {
        perror("server:fopen");
        return -1;
    }
    // set permission
    if(chmod(req->path, perm) == -1){
        fprintf(stderr, "chmod: cannot set permission for [%s]\n", req->path);
    }

    return 0;

}

/*
 * Writes inputs from client socket to destination file
 * on the server. 
 * Precondition: client has a valid input file stream 
 *      created using make_file
 * Postcondition: input file stream is closed only when 
 *      client has finished sending the file over 
 * Return
 * -- -1 on error
 * -- 0 if file copy not finished
 * -- fd if file copy finished
 * (i.e. file transfer over multiple select calls)
*/
int write_file(struct client *cli){

    struct request *req = &(cli->client_req);
    int fd = cli->fd;

    int nbytes = BUFSIZE;
    int num_read, num_wrote;
    char buf[BUFSIZE];

    num_read = read(fd, buf, nbytes);       

    if(num_read == -1) {
        perror("server:read");
        return -1;
    } else if(num_read != BUFSIZE){
        nbytes = num_read;
    }

    num_wrote = fwrite(buf, 1, nbytes, cli->file);

    if(num_wrote != nbytes){
        if(ferror(cli->file)){
            fprintf(stderr, "server:fwrite error at [%s]\n", req->path);
            return -1;
        }
    }

    // TODO: remove print lateer
    printf("%s <- %d bytes of [%s]\n", req->path, num_read, buf);

    // copy is finished if read
    // -- is successful
    // -- number of bytes read is not BUFSIZE
    if(nbytes != BUFSIZE || num_read == 0){

        if(fclose(cli->file) != 0){
            perror("server:fclose");
            return -1;
        }

        int response = OK;
        num_wrote = write(fd, &response, sizeof(int));      // TODO: error checking
        printf("%s (file) copy finished\n", req->path);     // TODO: remove print later
        return fd;
    }

    return 0;
}
