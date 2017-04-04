#ifndef _H_FUNC_H_
#define _H_FUNC_H_

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>

#include <netdb.h>
#include <sys/socket.h>

#include "hash.h"       // hash()
#include "ftree.h"      // request stuct

#define BUFSIZE 256

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
 * Returns
 * -- position of EOF in a char array buffer
 * -- -1 if not found
 */
int eof_pos(char *buffer);

/*
 * precondition: req.st_mode yields regular file
 * Sends data specified by req by
 * -- open file at req.path
 * -- write to client socket where nbytes is
 * ---- BUFSIZE if eof is not reached
 * ---- position of EOF if eof is reached
 */
void send_data(int fd, struct request *req);

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
 * Return
 * -- -1 for any error
 * -- 0 for success
 * -- >0 the number of child processes created
 */
int traverse(const char *source, int sock_fd, char *host, unsigned short port);


/*
 * The main client waits for count number of
 * child processes to terminate and report
 * -- nothing on success
 * -- error msg on error
 */
void client_wait(int count);

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
    FILE *file;
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
 * Return element before deleted item if found; NULL otherwise
 */
struct client *linkedlist_delete(struct client *head, int fd);

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

/*
 * Compare files
 * Based on client request (cli->client_req)
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
int compare_file(struct client *cli);

/*
 * Makes directory given client request with given
 * -- path
 * -- permission
 * Return -1 on error and fd if success
 */
int make_dir(struct client *cli);

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
int make_file(struct client *cli);

//TODO Description
int write_file(struct client *cli);

#endif // _H_FUNC_H_
