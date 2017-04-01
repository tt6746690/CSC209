#ifndef _H_FUNC_H_
#define _H_FUNC_H_

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include <netdb.h>
#include <sys/socket.h>

#include "hash.h"       // hash()
#include "ftree.h"      // request stuct 

/*********
 * CLIENT
 *********/

/* Create a new socket that connects to host 
 * Waiting for a successful connection
 * Returns sock_fd and exits should error arises
 */
int client_sock(char *host, unsigned short port);

/* Construct client request for file/dir at path
 * request is modified to accomodate changes 
 * exits process on error 
 */
void make_req(const char *path, struct request *client_req);

/*
 * Sends request struct to sock_fd over 5 read calls
 * In order of 
 * -- type 
 * -- path 
 * -- mode 
 * -- hash 
 * -- size
 */
void send_req(int sock_fd, struct request *req);

/*
 * Recursively traverses filepath rooted at source with sock_fd
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
void traverse(const char *source, int sock_fd, char *host, unsigned short port);


/*********
 * SERVER
 *********/

// linked list for tracking mult read for sending request struct 
struct client {
    int fd;
    int current_state;
    struct request client_req;
    struct client *next;
};

/*
 * Creates server socket 
 * binds to PORT and starts litening to 
 * connection from INADDR_ANY 
 */
int server_sock(unsigned short port);

// Reads request struct from client socket 
int handle_cli(int server_fd, int client_fd, struct client *client_req);

// Take a struct req (of type REGFILE/REGDIR) and determines whether a copy
// should be made (and tells the client).
int file_compare(int client_fd, struct request *req);

int copy_file();

#endif // _H_FUNC_H_
