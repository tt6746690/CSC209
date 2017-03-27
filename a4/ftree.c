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
  if(write(sock_fd, "Hello Internet\r\n", 17) == -1) {
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




}
