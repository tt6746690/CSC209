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


    /*printf("=== INFO ===\n");
    printf("req\t REGFILE=[1]\tREGDIR=[2]\tTRANSFILE=[3]\n");
    printf("res\t OK=[0] \tSENDFILE=[1]\tERROR=[2]\n");
    printf("\n");
    printf("=== Tree traversal ===\t\t\t\t "
            "=== Wait for copy to finish === \n"); 
    printf("pid \tsock \ttype \tres \tpath\t\t pid \tsize \tmode \tpath \thash\n");
	 */
    // tree traversal
    int child_count;
    // TODO: basename? 
    child_count = traverse(source, sock_fd, host, port);


    // close main socket for tree traversal
    close(sock_fd);

    // parent process wait for copy to finish
    if(child_count == -1){
        fprintf(stderr, "Error on traversal\n");
    } else if(child_count >= 0){
        client_wait(child_count);
    }

    return 0;
}


/*
 * Server handles incoming connection 
 */
void rcopy_server(unsigned short port){

    int sock_fd;
    sock_fd = server_sock(port);

    //printf("Server Starts listening on %d...\n", port);
    //printf("=== ACCEPTING === \n");
    //printf("sock\t activity \t state\n");


    // initialize empty fd set for accept
    int max_fd = sock_fd;
    fd_set all_fds, listen_fds;

    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    // head holds a linked list of client struct 
    struct client *head = malloc(sizeof(struct client));


    int stop = 30;

    while (stop--) {
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
         * Every new client occupies one node in linked list 
         */
        if (FD_ISSET(sock_fd, &listen_fds)) {

            int client_fd; 
            if ((client_fd = accept(sock_fd, NULL, NULL)) == -1) {
                perror("server: accept");
                continue;
            } 

            printf("%d \tcreate\t\n", client_fd);

            // update all_fds set 
            max_fd = (client_fd > max_fd) ? client_fd : max_fd;
            FD_SET(client_fd, &all_fds);

            // keep track of new client in head
            linkedlist_insert(head, client_fd);
        }


        /* Send proper response on active clients in linked list head
         * Note pointer p starts from head->next as the first valid client
         */
        for(struct client*p = head->next; p != NULL; p = p->next){
            if(FD_ISSET(p->fd, &listen_fds)){

                int result = read_req(p); 
					 printf("Result : %d and fd : %d\n", result, p->fd);
                /*
                 * result is 
                 * -- fd if
                 * ---- file transfer socket finish transfer file 
                 * ---- main socket finish traversing filepath (dunno how to check this)
                 * -- 0 to continue reading req  
                 * -- -1 if sys call fails
                 */
                if(result == -1){
                    fprintf(stderr, "ERROR: %s", (p->client_req).path);
                } else if(result == p->fd){
						  printf("RRR");
                    FD_CLR(p->fd, &all_fds);

                    // re-assign pointer p
                    if((p = linkedlist_delete(head, p->fd)) == NULL){
                        fprintf(stderr, "server:linkedlist_delete");
                    }

                    //printf("%d \tclosed \t\n", p->fd);

                } else{
                    //printf("%d \tcontinue \t%d\n", p->fd, p->current_state);
                }

            }
        }

        linkedlist_print(head);


    }
}


