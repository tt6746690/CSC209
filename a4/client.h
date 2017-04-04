/*********
 * CLIENT
 *********/

#ifndef _CLIENT_H_
#define _CLIENT_H_

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

#endif