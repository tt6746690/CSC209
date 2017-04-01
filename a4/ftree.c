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


