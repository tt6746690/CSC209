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

#include "ftree.h"

void handleclient(int server_fd, int client_fd, fd_set *listen_fds_ptr);

/*
 * Takes the file tree rooted at source, and copies transfers it to host
 */
int rcopy_client(char *source, char *host, unsigned short port){

  // The clients socket file descriptor.
  int sock_fd;
  struct sockaddr_in server;
  char buf[100];
  // Initilalze kaddr_in for server
  server.sin_family = PF_INET;
  server.sin_port = htons(port);
  //printf("PORT = %d\n", PORT);

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

  if(write(sock_fd, source, 17) == -1) {
	    perror("write");
	    exit(1);
	}
  if(read(sock_fd, buf, sizeof(buf)) <= 0) {
	    perror("read");
	    exit(1);
	}
  printf("SERVER ECHOED: %s\n", buf);

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

  // Bind the selected port to the socket.
  if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
      perror("server: bind");
      close(sock_fd);
      exit(1);
  }

  // Announce willingness to accept connections on this socket.
  // TODO: How many connections are we willing to take?
  if (listen(sock_fd, 100) < 0) {
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
              printf("Accepted connection\n");

        }

      }

      //TODO: Loop over the fds and read from the ready ones
      for(int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &listen_fds)) {
                handleclient(sock_fd, i, &listen_fds);

            }
      }
  }
}

void handleclient(int server_fd, int client_fd, fd_set *listen_fds_ptr){
  char message[MAXDATA + 1];
  message[0] = '\0';
  read(client_fd, message, sizeof(message));
  write(client_fd, message, sizeof(message));

  close(client_fd);
  FD_CLR(client_fd, listen_fds_ptr);

}
