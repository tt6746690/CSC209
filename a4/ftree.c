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

    // main socket for tree traversal 
    int sock_fd;
    sock_fd = connect_sock(host, port);

    traverse(source, sock_fd, host, port);

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


