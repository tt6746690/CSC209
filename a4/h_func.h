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

/* 
 * linked list for tracking mult read for sending request struct 
 *
 * current_state is one of
 * -- AWAITING_TYPE 0
 * -- AWAITING_PATH 1
 * -- AWAITING_SIZE 2
 * -- AWAITING_PERM 3
 * -- AWAITING_HASH 4
 * -- AWAITING_DATA 5
 */
struct client {
    int fd;
    int current_state;
    struct request client_req;
    struct client *next;
};

/*
 * Allocates memory for a new struct client 
 * at end of linked list with given fd 
 * Returns pointer to the newly created element 
 */
struct client *linkedlist_insert(struct client *head, int fd);

/*
 * Delete client in head linked list with given fd
 * Return 0 if found and -1 if not found
 */
int linkedlist_delete(struct client *head, int fd);

/*
 * Print linked list at head 
 * Each node is presented as fd 
 */
void linkedlist_print(struct client *head);

/*
 * Creates server socket 
 * binds to PORT and starts litening to 
 * connection from INADDR_ANY 
 */
int server_sock(unsigned short port);

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
 * -- 0 to continue reading req  
 * -- -1 if sys call fails
 */
int read_req(struct client *cli);

// Take a struct req (of type REGFILE/REGDIR) and determines whether a copy
// should be made (and tells the client).
int send_res(struct client *cli);


#endif // _H_FUNC_H_
