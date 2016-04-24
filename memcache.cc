/*
** memcache.cc -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <assert.h>

#include "memcache.h"

Memcache m(1024);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int
Memcache::accept_loop(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    // TODO: Install signal handler.

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s, fd: %d\n", s, new_fd);
        uint64_t cb_arg = new_fd;
        _tp.assign_task((void *)cb_arg);
    }

    return 0;
}

void
Memcache::read_socket(void *arg)
{
    uint64_t new_fd = (uint64_t) arg;
    cout << "fd: " << new_fd << endl;

    HEADER hdr = {0};
    int n = read(new_fd, &hdr, sizeof hdr);
    if (n < 0) {
        perror("read");
        close(new_fd);
        return;
    }
    printf("Read %d bytes from socket\n", n);
    printf("Magic %u, Opcode %u, Key Length: %u\n",
            hdr.magic, hdr.opcode, ntohs(hdr.key_length));
    unsigned key_length = ntohs(hdr.key_length);
    unsigned extra_length = hdr.extras_length;
    if (extra_length > 0) {
        uint8_t buf[256] = {0};
        n = read(new_fd, buf, extra_length);
        printf("n=%d, extra_length=%u\n", n, extra_length);
        assert(n == extra_length);
    }
    uint8_t key[256] = {0};
    n = read(new_fd, key, key_length);
    assert(n == key_length);
    printf("Key: %s\n", key);
    if (hdr.opcode == 0x01) {
        unsigned val_len = ntohl(hdr.total_body_length) - extra_length - key_length;
        uint8_t val[256] = {0};
        n = read(new_fd, val, val_len);
        assert(n == val_len);
        printf("Setting value: %s\n", val);
        m._c.set(key, val, val_len);
    } else if (hdr.opcode == 0x00) {
        void *val;
        size_t num_bytes;
        bool result = m._c.get(key, &val, &num_bytes);
        assert(result);
        char *valarr = (char *)val;
        for (int i = 0; i < num_bytes; i++) {
            printf("Got value from cache: %c\n", valarr[i]);
        }
    }
    close(new_fd);
}

void
Memcache::respond(int fd)
{
}


int main()
{
    m.init();
    m.accept_loop();

    return 0;
}
