#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <dirent.h>
#include <stdio.h>

#include "hash.h"
#include "ftree.h"

int handleclient(int server_fd, int client_fd, struct request *client_req);
void send_req(int sock_fd, struct request *re);
int file_compare(int client_fd, struct request *req);
int copy_file();
/*
 * Takes the file tree rooted at source, and copies transfers it to host
 */
int rcopy_client(char *source, char *host, unsigned short port){

    char buf[MAXDATA + 1];

    // Create clients socket.
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
    printf("PORT = %d\n", PORT);

    struct hostent *hp = gethostbyname(host);
    if ( hp == NULL ) {
        fprintf(stderr, "rcopy_client: %s unknown host\n", host);
        exit(1);
    }
    server.sin_addr = *((struct in_addr *)hp->h_addr);

    // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("client:connect"); close(sock_fd);
        exit(1);
    }

    //TODO: Make a loop over all files in the directory
    struct stat file_buf;
    if (lstat(source, &file_buf)){
        perror("lstat");
        exit(1);
    }

    if (S_ISDIR(file_buf.st_mode)){               // A dir
        DIR *dirp = opendir(source);
        struct dirent *dp;
        if (dirp == NULL){
            perror("opendir");
            exit(1);
        }

        while ((dp = readdir(dirp)) != NULL){
            if ((dp->d_name)[0] != '.'){          // avoid dot files 
                struct request client_req;

                // Compute "source/filename"
                int new_src_size = strlen(source) + sizeof('/') + strlen(dp->d_name) + 1;
                strncpy(client_req.path, source, strlen(source) + 1);
                strncat(client_req.path, "/", 1);
                strncat(client_req.path, dp->d_name, strlen(dp->d_name));
                client_req.path[new_src_size - 1] = '\0';

                // Compute file hash
                FILE *f = fopen(client_req.path,"r");
                if (f == NULL){
                    perror("fopen");
                    exit(1);
                }

                struct stat f_buf;
                if (lstat(client_req.path, &f_buf) == -1){
                    perror("lstat");
                    exit(1);
                }

                // Populate client_req
                hash(client_req.hash, f);
                client_req.mode = f_buf.st_mode;
                client_req.size = f_buf.st_size;

                if (S_ISDIR(f_buf.st_mode)){
                    client_req.type = REGDIR;
                } else{
                    client_req.type = REGFILE;
                }


                // Writes in this order: type, path, mode, hash, file size
                send_req(sock_fd, &client_req);

                int res_type;
                int num_read = read(sock_fd, &res_type, sizeof(int));
                //TODO: error check

                printf("Server response for %s : %d\n", client_req.path, res_type);

                if (res_type == SENDFILE){
                    printf("\tClient needs to send %s\n", client_req.path);
                    client_req.type = TRANSFILE;
                    send_req(sock_fd, &client_req);
                }

            }
        }


    } else if(S_ISREG(file_buf.st_mode)){             // is file


    }

    /*if(write(sock_fd, source, MAXDATA) == -1) {
      perror("write");
      exit(1);
      }
      if(read(sock_fd, buf, MAXDATA) <= 0) {
      perror("read");
      exit(1);
      }
      printf("SERVER ECHOED: %s\n", buf);*/

    if(close(sock_fd) == -1) {
        perror("close");
    }

    return -1;
}

void rcopy_server(unsigned short port){

    int on = 1, status;

    // Create the socket FD.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }

    // Make sure we can reuse the port immediately after the server terminates.
    status = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
            (const char *) &on, sizeof(on));
    if(status == -1) {
        perror("setsockopt -- REUSEADDR");
    }

    // Set information about the port (and IP) we want to be connected to.
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

    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAXCONNECTIONS) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }

    printf("Listening on %d\n", PORT);

    // The client accept - message accept loop. First, we prepare to listen to multiple
    // file descriptors by initializing a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    //int num_connections = 0;
    //struct request current_requests[MAXCONNECTIONS];
    while (1) {
        // select updates the fd_set it receives, 
        // so we always use a copy and retain the original.
        listen_fds = all_fds;

        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            printf("Calling accept\n");
            int client_fd = accept(sock_fd, NULL, NULL);

            if (client_fd < 0) {
                perror("server: accept");
                continue;
            }
            else {
                if (client_fd > max_fd) {
                    max_fd = client_fd;
                }
                FD_SET(client_fd, &all_fds);
                printf("Accepted connection %d\n", client_fd);
            }
        }

        // loop through fds
        for(int i = 0; i <= max_fd; i++) {
            // handle available sockets other than server sock_fd 
            if (FD_ISSET(i, &listen_fds) && i != sock_fd) {
                struct request client_req;
                if (handleclient(sock_fd, i, &client_req) == -1){
                    close(i);
                    FD_CLR(i, &all_fds);
                } else {
                    if (client_req.type == REGFILE){
                        printf("3. Type: %d at %p\n", client_req.type, &(client_req.type));
                        printf("path: %s at %p\n", client_req.path, &(client_req.path));
                        printf("mode: %d at %p\n", client_req.mode, &(client_req.mode));
                        printf("hash: %s at %p\n", client_req.hash, &(client_req.hash));
                        printf("size: %d at %p\n\n", client_req.size, &(client_req.size));
                        file_compare(i, &client_req);
                    } else if (client_req.type == REGDIR){
                        printf("3. Type: %d at %p\n", client_req.type, &(client_req.type));
                        printf("path: %s at %p\n", client_req.path, &(client_req.path));
                        printf("mode: %d at %p\n", client_req.mode, &(client_req.mode));
                        printf("hash: %s at %p\n", client_req.hash, &(client_req.hash));
                        printf("size: %d at %p\n\n", client_req.size, &(client_req.size));
                        file_compare(i, &client_req);
                    } else {  // client_req.type == TRANSFILE
                        printf("3. Type: %d at %p\n", client_req.type, &(client_req.type));
                        printf("path: %s at %p\n", client_req.path, &(client_req.path));
                        printf("mode: %d at %p\n", client_req.mode, &(client_req.mode));
                        printf("hash: %s at %p\n", client_req.hash, &(client_req.hash));
                        printf("size: %d at %p\n\n", client_req.size, &(client_req.size));
                        copy_file();
                    }
                }

            }
        }
    }
}


/*
 * Send request struct to client sock_fd
 */
void send_req(int sock_fd, struct request *req){

    if(write(sock_fd, &(req->type), sizeof(int)) == -1) {
        perror("write");
        exit(1);
    }
    if(write(sock_fd, req->path, MAXPATH) == -1) {
        perror("write");
        exit(1);
    }
    if(write(sock_fd, &(req->mode), sizeof(mode_t)) == -1) {
        perror("write");
        exit(1);
    }
    if(write(sock_fd, req->hash, BLOCKSIZE) == -1) {
        perror("write");
        exit(1);
    }
    if(write(sock_fd, &(req->size), sizeof(size_t)) == -1) {
        perror("write");
        exit(1);
    }

    /*printf("1. Type: %d at %p\n", re->type, &(re->type));
      printf("path: %s at %p\n", re->path, &(re->path));
      printf("mode: %d at %p\n", re->mode, &(re->mode));
      printf("hash: %s at %p\n", re->hash, &(re->hash));
      printf("size: %d at %p\n\n", re->size, &(re->size));*/
}


/*
 * Reads the 5 pieces of information from the client.
 */
int handleclient(int server_fd, int client_fd, struct request *client_req){

    // About to receive data in order: type, path, mode, hash, size
    int num_read = read(client_fd, &(client_req->type), sizeof(int));
    if (num_read == 0){
        return -1;
    }

    num_read = read(client_fd, client_req->path, MAXPATH);
    if (num_read == 0){
        return -1;
    }
    printf("Looking at: %s\n", client_req->path);

    num_read = read(client_fd, &(client_req->mode), sizeof(mode_t));
    if (num_read == 0){
        return -1;
    }

    num_read = read(client_fd, client_req->hash, BLOCKSIZE);
    if (num_read == 0){
        return -1;
    }

    num_read = read(client_fd, &(client_req->size), sizeof(size_t));
    if (num_read == 0){
        return -1;
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
