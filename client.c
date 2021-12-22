#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BUF 256

void Usage(char *progname);

int LookupName(char *name,
                unsigned short port,
                struct sockaddr_storage *ret_addr,
                size_t *ret_addrlen);

int Connect(const struct sockaddr_storage *addr,
             const size_t addrlen,
             int *ret_fd);
	     
void PrintOut(int fd, struct sockaddr *addr, size_t addrlen);

int 
main(int argc, char **argv) {
  if (argc != 3) {
    Usage(argv[0]);
  }

  unsigned short port = 0;
  if (sscanf(argv[2], "%hu", &port) != 1) {
    Usage(argv[0]);
  }

  // Get an appropriate sockaddr structure.
  struct sockaddr_storage addr;
  size_t addrlen;
  if (!LookupName(argv[1], port, &addr, &addrlen)) {
    Usage(argv[0]);
  }

  // Connect to the remote host.
  int socket_fd;
  if (!Connect(&addr, addrlen, &socket_fd)) {
    Usage(argv[0]);
  }
  
  PrintOut(socket_fd, (struct sockaddr *)(&addr), addrlen);

  char* line = NULL;
  size_t len = 0;
  char readbuf[BUF];

  while (1) {
    // Read from stdin
    getline(&line, &len, stdin);
    if (line == NULL)
      continue; //continue the loop if there is no input
    if (strcmp(line, "exit\n") == 0) 
      break;
    //Write to server
    while (1) {
      int wres = write(socket_fd, line, len);
      if (wres == 0) {
       printf("socket closed prematurely \n");
        close(socket_fd);
        return EXIT_FAILURE;
      }
      if (wres == -1) {
        if (errno == EINTR)
          continue;
        printf("socket write failure \n");
        close(socket_fd);
        return EXIT_FAILURE;
      }
      break;
    }
    
    //Read from server and output to stdout
    int res = 0;
    while (1) {
      res = read(socket_fd, readbuf, BUF-1);
      if (res == 0) {
        printf("socket closed prematurely \n");
        close(socket_fd);
        return EXIT_FAILURE;
      }
      if (res == -1) {
        if (errno == EINTR)
          continue;
        printf("socket read failure \n");
        close(socket_fd);
        return EXIT_FAILURE;
      }
      break;
    }
    readbuf[res] = '\0';
    printf("%s", readbuf);
  }

  // Clean up.
  close(socket_fd);
  return EXIT_SUCCESS;
}

void 
Usage(char *progname) {
  printf("usage: %s  hostname port \n", progname);
  exit(EXIT_FAILURE);
}

int 
LookupName(char *name,
                unsigned short port,
                struct sockaddr_storage *ret_addr,
                size_t *ret_addrlen) {
  struct addrinfo hints, *results;
  int retval;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  // Do the lookup by invoking getaddrinfo().
  if ((retval = getaddrinfo(name, NULL, &hints, &results)) != 0) {
    printf( "getaddrinfo failed: %s\n", gai_strerror(retval));
    return 0;
  }

  // Set the port in the first result.
  if (results->ai_family == AF_INET) {
    struct sockaddr_in *v4addr =
            (struct sockaddr_in *) (results->ai_addr);
    v4addr->sin_port = htons(port);
  } else if (results->ai_family == AF_INET6) {
    struct sockaddr_in6 *v6addr =
            (struct sockaddr_in6 *)(results->ai_addr);
    v6addr->sin6_port = htons(port);
  } else {
    printf("getaddrinfo failed to provide an IPv4 or IPv6 address \n");
    freeaddrinfo(results);
    return 0;
  }

  // Return the first result.
  assert(results != NULL);
  memcpy(ret_addr, results->ai_addr, results->ai_addrlen);
  *ret_addrlen = results->ai_addrlen;

  // Clean up.
  freeaddrinfo(results);
  return 1;
}

int 
Connect(const struct sockaddr_storage *addr,
             const size_t addrlen,
             int *ret_fd) {
  // Create the socket.
  int socket_fd = socket(addr->ss_family, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    printf("socket() failed: %s\n", strerror(errno));
    return 0;
  }

  // Connect the socket to the remote host.
  int res = connect(socket_fd,
                    (const struct sockaddr *)(addr),
                    addrlen);
  if (res == -1) {
    printf("connect() failed: %s\n", strerror(errno));
    return 0;
  }

  *ret_fd = socket_fd;
  return 1;
}

void
PrintOut(int fd, struct sockaddr *addr, size_t addrlen) {
  printf("established connection with server ");
    if (addr->sa_family == AF_INET) {
        // Print out the IPV4 address and port
        char astring[INET_ADDRSTRLEN];
	struct sockaddr_in *in4 = (struct sockaddr_in *)(addr);
	inet_ntop(AF_INET, &(in4->sin_addr), astring, INET_ADDRSTRLEN);
	printf("%s\n", astring);
    } else if (addr->sa_family == AF_INET6) {
        // Print out the IPV6 address and port
	char astring[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)(addr);
	inet_ntop(AF_INET6, &(in6->sin6_addr), astring, INET6_ADDRSTRLEN);
	printf("%s\n", astring);
    } else {
        printf(" ???? address and port ???? \n");
    }
}
	
