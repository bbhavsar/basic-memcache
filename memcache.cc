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
#include "assert_fail.h"

Memcache m(1024);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Wrapper around read call that assert fails
// on error and reads in loop till required
// bytes are read.
static void
read_bytes(int fd, void *buffer, size_t len)
{
    char *buf = (char *)buffer;
    unsigned int bytes_read = 0;
    while (len > 0) {
        int n = read(fd, buf + bytes_read, len);
        ASSERT(n >= 0, "read error");
        bytes_read += n;
        len -= n;
    }
}

// Wrapper around write call that assert fails
// on error and writes in loop till required
// bytes are read.
static void
write_bytes(int fd, void *buffer, size_t len)
{
    char *buf = (char *)buffer;
    unsigned int bytes_written = 0;
    while (len > 0) {
        int n = send(fd, buf + bytes_written, len, 0);
        ASSERT(n >= 0, "write error");
        printf("Bytes written %d\n", n);
        bytes_written += n;
        len -= n;
    }
}

// Print contents of buffer one char at a time
// since it's not a null terminated string.
// Avoids copying buffer.
static void
print_buf(char *s, size_t len)
{
    printf("Buffer ");
    for (int i = 0; i < len; i++) {
        printf("%c", s[i]);
    }
    printf("\n");
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
        memset(&their_addr, 0, sizeof their_addr);
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            fprintf(stderr, "Error in accept\n");
            exit(1);
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
    uint64_t fd = (uint64_t) arg;

    // Read header from the request.
    HEADER hdr = {0};

    read_bytes(fd, (void *)&hdr, sizeof hdr);
    printf("Magic %u, Opcode %u, Key Length: %u, CAS: %lu\n",
            hdr.magic, hdr.opcode, ntohs(hdr.key_length), ntohl(hdr.cas));
    unsigned key_length = ntohs(hdr.key_length);
    unsigned extra_length = hdr.extras_length;
    if (extra_length > 0) {
        uint8_t buf[extra_length];
        memset(buf, 0, extra_length);
        read_bytes(fd, (void *)buf, extra_length);
    }

    // Read key
    char *key_buf = malloc(key_length);
    ASSERT(key_buf, "Failed to allocate key buf");
    memset(key_buf, 0, key_length);

    read_bytes(fd, (void *)key_buf, key_length);
    string key(key_buf, key_length);

    printf("Key: %s\n", key.c_str());

    // Action
    switch (hdr.opcode) {
    case 0x01:  {// Set
        unsigned val_len = ntohl(hdr.total_body_length) - extra_length - key_length;
        void *val = malloc(val_len);
        ASSERT(val, "Failed to allocate value buf");
        memset(val, 0, val_len);
        read_bytes(fd, val, val_len);
        print_buf((char *)val, val_len);
        m._c.set(key, val, val_len);
        free(val);
        m.respond_to_set(fd);
        break;
    }
    case 0x00:  {// Get
        void *val;
        size_t num_bytes;
        bool result = m._c.get(key, &val, &num_bytes);
        if (result) {
            print_buf((char *)val, num_bytes);
        }
        m.respond_to_get(fd, result, val, num_bytes);
        break;
    }
    default:
        fprintf(stderr, "Unimplemented opcode 0x%x\n", hdr.opcode);
        exit(1);
    }

    free(key_buf);
    close(fd);
}

void
Memcache::respond_to_get(int fd, bool available, void *val, size_t len)
{
    HEADER hdr = {0};
    hdr.magic = 0x81;
    hdr.opcode = 0x00;
    hdr.key_length = 0x0;
    hdr.extras_length = 0x4;
    hdr.data_type = 0;

    hdr.status = htons(!available);
    if (!available) {
        char not_found_str[] = "Not found";
        val = not_found_str;
        len = sizeof not_found_str;
    }
    hdr.total_body_length = htonl(len + hdr.extras_length);

    write_bytes(fd, &hdr, sizeof hdr);
    uint32_t extra = 0;
    write_bytes(fd, (void *)&extra, sizeof extra);
    write_bytes(fd, val, len);
}

void
Memcache::respond_to_set(int fd)
{
    HEADER hdr = {0};
    hdr.magic = 0x81;
    hdr.opcode = 0x01;
    hdr.key_length = 0x0;
    hdr.extras_length = 0;
    hdr.data_type = 0;
    hdr.status = 0;
    hdr.total_body_length = 0;
    write_bytes(fd, &hdr, sizeof hdr);
}

int main()
{
    m.init();
    m.accept_loop();

    return 0;
}
