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

#include "h_func.h"




/*
 * Takes the file tree rooted at source, and copies transfers it to host
 */
int rcopy_client(char *source, char *host, unsigned short port){
    static int depth = 0;

    struct stat file_buf;
    if (lstat(source, &file_buf)){
        perror("lstat");
        exit(1);
    }

    struct request client_req;
    make_req(source, &client_req);
    
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
    //printf("PORT = %d\n", PORT);

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

    // Writes in this order: type, path, mode, hash, file size
    send_req(sock_fd, &client_req);

    int res_type;
    int num_read = read(sock_fd, &res_type, sizeof(int));
    //TODO: error check

    printf("Server response for %s : %d\n", client_req.path, res_type);

    if (res_type == SENDFILE){
        printf("\tClient needs to send %s\n", client_req.path);
        int result = fork();
        if (result > 0){ // Child
          int sock_fd_child;
          sock_fd_child = socket(AF_INET, SOCK_STREAM, 0);
          // Set the IP and port of the server to connect to.
          struct sockaddr_in server_child;
          server_child.sin_family = PF_INET;
          server_child.sin_port = htons(port);
          //printf("PORT = %d\n", PORT);

          server_child.sin_addr = *((struct in_addr *)hp->h_addr);

          // Connect to server
          if (connect(sock_fd_child, (struct sockaddr *)&server_child, sizeof(server_child)) == -1) {
              perror("client:connect"); close(sock_fd);
              exit(1);
          }
          // Send same request, with different type.
          client_req.type = TRANSFILE;
          send_req(sock_fd_child, &client_req);

          // TODO: then send in file without expecting a message 
          // then wait for OK message  
          exit(1);
        } else if (result < 0){
          perror("fork");
          exit(1);
        }
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

                // Compute "source/filename"
                int new_src_size = strlen(source) + sizeof('/') + strlen(dp->d_name) + 1;
                char new_src[new_src_size];
                strncpy(new_src, source, strlen(source) + 1);
                strncat(new_src, "/", 1);
                strncat(new_src, dp->d_name, strlen(dp->d_name));
                new_src[new_src_size - 1] = '\0';

                rcopy_client(new_src, host, port);

            }
        }


    } else if(S_ISREG(file_buf.st_mode)){             // is file


    }

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

    struct client *head = NULL;
    while (1) {
        // select updates the fd_set it receives,
        // so we always use a copy and retain the original.
        listen_fds = all_fds;

        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Create incoming new connection 
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
                struct client *temp = head;
                head = (struct client *)malloc(sizeof(struct client));
                if (head == NULL){
                  perror("malloc");
                  exit(1);
                }
                head->fd = client_fd;
                head->current_state = AWAITING_TYPE;
                head->next = temp;
                (head->client_req).type = REGFILE; // TODO: JUST a test delete later
                printf("Accepted connection %d\n", client_fd);
            }
        }

        for(struct client*p = head; p != NULL; p = p->next){
            if(FD_ISSET(p->fd, &listen_fds)){

                int ret = handle_cli(sock_fd, p->fd, p); 
                if (ret == -1){
                    FD_CLR(p->fd, &all_fds);
                    printf("Connection %d closed\n", p->fd);
                } else{
                    printf("Connection %d in state [%d]\n", p->fd, p->current_state);
                }
            }
        }

    }
}


