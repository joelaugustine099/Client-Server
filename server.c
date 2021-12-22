#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

void Usage(char *progname);
void PrintOut(int fd, struct sockaddr *addr, size_t addrlen);
int  Listen(char *portnum, int *sock_family);
void* HandleClient(void* params);
struct clientParams {
  int c_fd;
  struct sockaddr* addr;
  size_t addrlen;
  int sock_family;
};

int 
main(int argc, char **argv) {
  // Expect the port number as a command line argument.
  if (argc != 2) {
    Usage(argv[0]);
  }

  int sock_family;
  int listen_fd = Listen(argv[1], &sock_family);
  if (listen_fd <= 0) {
    // We failed to bind/listen to a socket.  Quit with failure.
    printf("Couldn't bind to any addresses.\n");
    return EXIT_FAILURE;
  }

  // Loop forever, accepting a connection from a client and running HandleClient
  while (1) {
    struct sockaddr_storage caddr;
    socklen_t caddr_len = sizeof(caddr);
    int client_fd = accept(listen_fd,
                           (struct sockaddr *)(&caddr),
                           &caddr_len);
    if (client_fd < 0) {
      if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK))
        continue;
      printf("Failure on accept:%d \n ", strerror(errno));
      break;
    }
    struct clientParams cparams;
    cparams.c_fd = client_fd;
    cparams.addr = (struct sockaddr *)(&caddr);
    cparams.addrlen = caddr_len;
    cparams.sock_family = sock_family;

    pthread_t p1;
    assert(pthread_create(&p1, NULL, HandleClient, &cparams) == 0);
  }

  // Close socket
  close(listen_fd);
  return EXIT_SUCCESS;
}

void Usage(char *progname) {
  printf("usage: %s port \n", progname);
  exit(EXIT_FAILURE);
}

void 
PrintOut(int fd, struct sockaddr *addr, size_t addrlen) {
  printf("established connection with client ");
  if (addr->sa_family == AF_INET) {
    // Print out the IPV4 address and port

    char astring[INET_ADDRSTRLEN];
    struct sockaddr_in *in4 = (struct sockaddr_in *)(addr);
    inet_ntop(AF_INET, &(in4->sin_addr), astring, INET_ADDRSTRLEN);
    printf("%s", astring);
    printf("#%d\n", ntohs(in4->sin_port));

  } else if (addr->sa_family == AF_INET6) {
    // Print out the IPV6 address and port

    char astring[INET6_ADDRSTRLEN];
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)(addr);
    inet_ntop(AF_INET6, &(in6->sin6_addr), astring, INET6_ADDRSTRLEN);
    printf("%s", astring);
    printf("#%d\n", ntohs(in6->sin6_port));

  } else {
    printf(" ???? address and port ???? \n");
  }
}

int 
Listen(char *portnum, int *sock_family) {

  // Populate the "hints" addrinfo structure for getaddrinfo().
  // ("man addrinfo")
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;       // IPv6 (also handles IPv4 clients)
  hints.ai_socktype = SOCK_STREAM;  // stream
  hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
  hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
  hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  // Use argv[1] as the string representation of our portnumber to
  // pass in to getaddrinfo().  getaddrinfo() returns a list of
  // address structures via the output parameter "result".
  struct addrinfo *result;
  int res = getaddrinfo(NULL, portnum, &hints, &result);

  // Did addrinfo() fail?
  if (res != 0) {
	printf( "getaddrinfo failed: %s", gai_strerror(res));
    return -1;
  }

  // Loop through the returned address structures until we are able
  // to create a socket and bind to one.  The address structures are
  // linked in a list through the "ai_next" field of result.
  int listen_fd = -1;
  struct addrinfo *rp;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    listen_fd = socket(rp->ai_family,
                       rp->ai_socktype,
                       rp->ai_protocol);
    if (listen_fd == -1) {
      // Creating this socket failed.  So, loop to the next returned
      // result and try again.
      printf("socket() failed:%d \n ", strerror(errno));
      listen_fd = -1;
      continue;
    }

    // Configure the socket; we're setting a socket "option."  In
    // particular, we set "SO_REUSEADDR", which tells the TCP stack
    // so make the port we bind to available again as soon as we
    // exit, rather than waiting for a few tens of seconds to recycle it.
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    // Try binding the socket to the address and port number returned
    // by getaddrinfo().
    if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      // Bind worked!
      // Return to the caller the address family.
      *sock_family = rp->ai_family;
      break;
    }

    // The bind failed.  Close the socket, then loop back around and
    // try the next address/port returned by getaddrinfo().
    close(listen_fd);
    listen_fd = -1;
  }

  // Free the structure returned by getaddrinfo().
  freeaddrinfo(result);

  // If we failed to bind, return failure.
  if (listen_fd == -1)
    return listen_fd;

  // Success. Tell the OS that we want this to be a listening socket.
  if (listen(listen_fd, SOMAXCONN) != 0) {
    printf("Failed to mark socket as listening:%d \n ", strerror(errno));
    close(listen_fd);
    return -1;
  }

  // Return to the client the listening file descriptor.
  return listen_fd;
}

void* 
HandleClient(void* params) {
  // Print out information about the client.
  struct clientParams* cparams = (struct clientParams*) params;
  int tid = syscall(SYS_gettid);
  printf("worker %d: ", tid);
  int c_fd = (*cparams).c_fd;
  struct sockaddr* addr = (*cparams).addr;
  size_t addrlen = (*cparams).addrlen;
  int sock_family = (*cparams).sock_family;

  PrintOut(c_fd, addr, addrlen);

  // Loop, reading data and sends word and character count back, until the client
  // closes the connection.
  while (1) {
    char clientbuf[1024];
    ssize_t res = read(c_fd, clientbuf, 1023);
    if (res == 0) {
      printf("worker %d: client terminated\n", tid);
      break;
    }

    if (res == -1) {
      if ((errno == EAGAIN) || (errno == EINTR))
        continue;

	  printf(" Error on client socket:%d \n ", strerror(errno));
      break;
    }
    clientbuf[res] = '\0';

    int wc = 0;
    int cc = 0;
    int word = 0;
    int i;
    for (i = 0; i < strlen(clientbuf)-1; i++) {
      if (clientbuf[i] == ' ') {
	if (word) {
	  word = 0;
	  wc++;
	}
      } else {
	  word = 1;
	  cc++;
	  if (i == strlen(clientbuf)-2)
	    wc++;
      }
    }
    printf("worker %d: received message from client. # words = %d and # characters = %d\n", tid, wc, cc);
    char s[10];
    sprintf(s, "%d %d\n", wc, cc);
    write(c_fd, s, strlen(s));

  }

  close(c_fd);
  return NULL;
}
