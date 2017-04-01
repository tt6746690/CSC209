#ifndef _H_FUNC_H_
#define _H_FUNC_H_

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "hash.h"       // hash()
#include "ftree.h"      // request stuct 


/* Client */

// Construct client request for file/dir at path
void make_req(char *path, struct request *client_req);

// Sends request struct to sock_fd over 5 read calls 
void send_req(int sock_fd, struct request *req);



/* Server */

// linked list for tracking mult read for sending request struct 
struct client {
    int fd;
    int current_state;
    struct request client_req;
    struct client *next;
};


// Reads request struct from client socket 
int handle_cli(int server_fd, int client_fd, struct client *client_req);

// Take a struct req (of type REGFILE/REGDIR) and determines whether a copy
// should be made (and tells the client).
int file_compare(int client_fd, struct request *req);

int copy_file();

#endif // _H_FUNC_H_
