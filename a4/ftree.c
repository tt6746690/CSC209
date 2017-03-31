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

int handleclient(int server_fd, int client_fd, struct request *client_req);
void send_struct(int sock_fd, struct request *re);
int file_compare(int client_fd, struct request *req);
int copy_file();
/*
 * Takes the file tree rooted at source, and copies transfers it to host
 */
int rcopy_client(char *source, char *host, unsigned short port){

  // The clients socket file descriptor.
  int sock_fd;
  struct sockaddr_in server;
  char buf[MAXDATA + 1];
  // Initilalze kaddr_in for server
  server.sin_family = PF_INET;
  server.sin_port = htons(port);
  printf("PORT = %d\n", PORT);

  struct hostent *hp = gethostbyname(host);
  if ( hp == NULL ) {
    fprintf(stderr, "rcopy_client: %s unknown host\n", host);
      exit(1);
  }
  server.sin_addr = *((struct in_addr *)hp->h_addr);


  // Creates a socket
  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
      perror("client: socket");
      exit(1);
  }

  /* request connection to server */
  if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
      perror("client:connect"); close(sock_fd);
      exit(1);
  }

  //TODO: Make a loop over all files in the directory
  struct stat file;
  if (lstat(source, &file)){
    perror("lstat");
    exit(1);
  }
  // If a directory
  if ((file.st_mode & S_IFMT) == S_IFDIR){
    printf("%s\n", source);
    DIR *dirp = opendir(source);
    struct dirent *dp;
    if (dirp == NULL){
      perror("opendir");
      exit(1);
    }

    while ((dp = readdir(dirp)) != NULL){
      if ((dp->d_name)[0] != '.'){
        struct request client_req;

        // Makes the filename "source/filename".
        int new_src_size = strlen(source) + strlen(dp->d_name) + 2;
        //char new_src[new_src_size];
        strncpy(client_req.path, source, strlen(source) + 1);
        strncat(client_req.path, "/", 1);
        strncat(client_req.path, dp->d_name, strlen(dp->d_name));
        client_req.path[new_src_size - 1] = '\0';
        //printf("Will send %s\n", client_req.path);

        FILE *sub_file_f = fopen(client_req.path,"r");
        if (sub_file_f == NULL){
          perror("fopen");
          exit(1);
        }


        // TODO: replace with fields from dirent?
        struct stat sub_file;
        if (lstat(client_req.path, &sub_file) == -1){
          perror("lstat");
          exit(1);
        }

        hash(client_req.hash, sub_file_f);

        client_req.mode = sub_file.st_mode;

        client_req.size = sub_file.st_size;

        if ((sub_file.st_mode & S_IFMT) == S_IFDIR)
          client_req.type = REGDIR;
        else
          client_req.type = REGFILE;

          // Writes in this order: type, path, mode, hash, file size
        send_struct(sock_fd, &client_req);

        int next_step;
        int num_read = read(sock_fd, &next_step, sizeof(int));
        //TODO: error check

        printf("Server response for %s : %d\n", client_req.path, next_step);

        if (next_step == SENDFILE){
          printf("\tClient needs to send %s\n", client_req.path);
          client_req.type = TRANSFILE;
          send_struct(sock_fd, &client_req);
        }


        /*char file_b[MAXDATA];
        if(read(sock_fd, file_b, MAXDATA) <= 0) {
      	    perror("read");
      	    exit(1);
      	}

        printf("SERVER ECHOED: %s\n", file_b);*/

      }
      //dp = readdir(dirp);
    }


  } else if ((file.st_mode & S_IFMT) == S_IFREG){


  }

  /*if(write(sock_fd, source, MAXDATA) == -1) {
	    perror("write");
	    exit(1);
	}
  if(read(sock_fd, buf, MAXDATA) <= 0) {
	    perror("read");
	    exit(1);
	}
  printf("SERVER ECHOED: %s\n", buf);*/

  if(close(sock_fd) == -1) {
	    perror("close");
	}

  return -1;
}

void rcopy_server(unsigned short port){

  // Create the socket FD.
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
      perror("server: socket");
      exit(1);
  }

  // TODO setsockopt for reusing port?

  // Set information about the port (and IP) we want to be connected to.
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  server.sin_addr.s_addr = INADDR_ANY;
  memset(&server.sin_zero, 0, 8);

  printf("Listening on %d\n", PORT);


  // Bind the selected port to the socket.
  if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
      perror("server: bind");
      close(sock_fd);
      exit(1);
  }

  // Announce willingness to accept connections on this socket.
  // TODO: How many connections are we willing to take?
  if (listen(sock_fd, 5) < 0) {
      perror("server: listen");
      close(sock_fd);
      exit(1);
  }

  // The client accept - message accept loop. First, we prepare to listen to multiple
  // file descriptors by initializing a set of file descriptors.
  int max_fd = sock_fd;
  fd_set all_fds, listen_fds;
  FD_ZERO(&all_fds);
  FD_SET(sock_fd, &all_fds);

  //int num_connections = 0;
  //struct request current_requests[MAXCONNECTIONS];
  while (1) {
      // select updates the fd_set it receives, so we always use a copy and retain the original.
      listen_fds = all_fds;
      int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
      if (nready == -1) {
          perror("server: select");
          exit(1);
      }

      // Is it the original socket? Create a new connection ...
      if (FD_ISSET(sock_fd, &listen_fds)) {
      	  printf("Calling accept\n");
          int client_fd = accept(sock_fd, NULL, NULL);
          if (client_fd < 0) {
              perror("server: accept");
              close(sock_fd);
              //exit(1);
          }
          else {
              if (client_fd > max_fd) {
                max_fd = client_fd;
              }
              FD_SET(client_fd, &all_fds);
              printf("Accepted connection %d\n", client_fd);
              /*char message[MAXDATA + 1];
              //message[0] = '\0';
              int num_read = read(client_fd, message, MAXDATA);
              message[num_read] = '\0';
              printf("SERVER RECEIVED: %s\n", message);
              write(client_fd, message, MAXDATA);*/


          }

      }

      //TODO: Loop over the fds and read from the ready ones
      for(int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &listen_fds)) {
                if (i != sock_fd){
                  struct request client_req;
                  if (handleclient(sock_fd, i, &client_req) == -1){
                    close(i);
                    FD_CLR(i, &all_fds);
                  } else {
                      if (client_req.type == REGFILE){
                        file_compare(i, &client_req);
                      } else if (client_req.type == REGDIR){
                        file_compare(i, &client_req);
                      } else {  // client_req.type == REGDIR
                        printf("3. Type: %d at %p\n", client_req.type, &(client_req.type));
                        printf("path: %s at %p\n", client_req.path, &(client_req.path));
                        printf("mode: %d at %p\n", client_req.mode, &(client_req.mode));
                        printf("hash: %s at %p\n", client_req.hash, &(client_req.hash));
                        printf("size: %d at %p\n\n", client_req.size, &(client_req.size));
                        copy_file();
                      }
                  }
                  //FD_CLR(i, &listen_fds);
                }

            }
      }
  }
}

void send_struct(int sock_fd, struct request *re){

  if(write(sock_fd, &(re->type), sizeof(int)) == -1) {
      perror("write");
      exit(1);
  }
  if(write(sock_fd, re->path, MAXPATH) == -1) {
      perror("write");
      exit(1);
  }
  if(write(sock_fd, &(re->mode), sizeof(mode_t)) == -1) {
      perror("write");
      exit(1);
  }
  if(write(sock_fd, re->hash, BLOCKSIZE) == -1) {
      perror("write");
      exit(1);
  }
  if(write(sock_fd, &(re->size), sizeof(size_t)) == -1) {
      perror("write");
      exit(1);
  }

  /*printf("1. Type: %d at %p\n", re->type, &(re->type));
  printf("path: %s at %p\n", re->path, &(re->path));
  printf("mode: %d at %p\n", re->mode, &(re->mode));
  printf("hash: %s at %p\n", re->hash, &(re->hash));
  printf("size: %d at %p\n\n", re->size, &(re->size));*/


}


/*
 * Reads the 5 pieces of information from the client.
*/
int handleclient(int server_fd, int client_fd, struct request *client_req){

  // About to receive data in order: type, path, mode, hash, size
  int num_read = read(client_fd, &(client_req->type), sizeof(int));
  if (num_read == 0){
    return -1;
  }

  num_read = read(client_fd, client_req->path, MAXPATH);
  if (num_read == 0){
    return -1;
  }
  printf("Looking at: %s\n", client_req->path);

  num_read = read(client_fd, &(client_req->mode), sizeof(mode_t));
  if (num_read == 0){
    return -1;
  }

  num_read = read(client_fd, client_req->hash, BLOCKSIZE);
  if (num_read == 0){
    return -1;
  }

  num_read = read(client_fd, &(client_req->size), sizeof(size_t));
  if (num_read == 0){
    return -1;
  }
  //printf("%s\n", "Successful information transfer");

  /*printf("2. Type: %d at %p\n", client_req->type, &(client_req->type));
  printf("path: %s at %p\n", client_req->path, &(client_req->path));
  printf("mode: %d at %p\n", client_req->mode, &(client_req->mode));
  printf("hash: %s at %p\n", client_req->hash, &(client_req->hash));
  printf("size: %d at %p\n", client_req->size, &(client_req->size));*/


  return client_fd;
}
/*
 * Take a struct req (of type REGFILE/REGDIR) and determines whether a copy
 * should be made (and tells the client).
 */
int file_compare(int client_fd, struct request *req){
    //struct stat server_file_stat;
    struct request new_request = *req;
    FILE *server_file = fopen(new_request.path, "r");
    if (server_file == NULL && errno != ENOENT){
      perror("fopen");
      exit(1);
    }
    int compare = 0;
    if (server_file != NULL){
      char file_hash[BLOCKSIZE];
      printf("%s exist on server\n", new_request.path);
      if (chmod(new_request.path, new_request.mode) == -1){
        perror("chmod");
        exit(1);
      }
      hash(file_hash, server_file);
      compare = check_hash(new_request.hash, file_hash);
      //show_hash(new_request.hash);
      //show_hash(file_hash);
    }
    int response = 0;
    if (compare || server_file == NULL){
      printf("Gotta copy %s\n", new_request.path);
      printf("\tcomp = %d\n", compare);
      if (server_file == NULL){
        printf("\t NULL!");
      }
      response = SENDFILE;
    } else{
      response = OK;

    }
    write(client_fd, &response, sizeof(int));
    return 0;
}

int copy_file(){

 return 0;

}
