#include <string.h>
#include <errno.h>
#include <stdlib.h>
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
    sock_fd = client_sock(host, port);

    traverse(source, sock_fd, host, port);

    printf("=== INFO ===\n");
    fprintf(stderr, "req.type: REGFILE=[1]; REGDIR=[2]; TRANSFILE=[3]\n");
    fprintf(stderr, "res.type: OK=[0]; SENDFILE=[1]; ERROR=[2]\n");

    return -1;
}


/*
 * Server handles incoming connection 
 */
void rcopy_server(unsigned short port){

    int sock_fd;
    sock_fd = server_sock(port);

    printf("Server Starts listening on %d...\n", port);

    // initialize empty fd set for accept
    int max_fd = sock_fd;
    fd_set all_fds, listen_fds;

    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    // head holds a linked list of client struct 
    struct client *head = NULL;

    while (1) {
        /* select updates the fd_set it receives,
         * so we always use a copy and retain the original.
         */
        listen_fds = all_fds;

        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        /* On active server socket, accept incoming client connection
         * Every new client occupies one node in linked list head 
         */
        if (FD_ISSET(sock_fd, &listen_fds)) {

            int client_fd; 
            if ((client_fd = accept(sock_fd, NULL, NULL)) == -1) {
                perror("server: accept");
                continue;
            } 

            // update all_fds set 
            max_fd = (client_fd > max_fd) ? client_fd : max_fd;
            FD_SET(client_fd, &all_fds);

            // keep track of new client in head
            linkedlist_insert(head, client_fd);
        }


        // Send proper response on active clients in linked list head
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


