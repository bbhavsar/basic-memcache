/*
 * memcache.cc -- a stream socket server demo
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

static const char *PORT = "11211";
static const int BACKLOG = 10;

static Memcache m(1024 /* capacity_bytes */);

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
// bytes are read except for EOF.
// Returns true on EOF.
static bool
read_bytes(int fd, void *buffer, size_t len)
{
    char *buf = (char *)buffer;
    unsigned int bytes_read = 0;
    while (len > 0) {
        int n = read(fd, buf + bytes_read, len);
        ASSERT(n >= 0, "read error");
        if (n == 0) {
            // EOF
            return true;
        }
        bytes_read += n;
        len -= n;
    }
    return false;
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
Memcache::event_loop(void)
{
    fd_set master;  // temp file descriptor list for select()
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }
        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, BACKLOG) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // Main event loop
    // New connections are accepted and from
    // existing connection only header is read.
    // Once header is read, acting on the opcode
    // getting/setting data from the cache
    // is handled by one of the worker threads
    // in the threadpool.
    // This way main event loop thread does
    // minimal work and allows multiple connections
    // to get/set key values simultaneously.
    for(;;) {
        read_fds = master; // copy it

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_in_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                    }
                } else {
                    pthread_mutex_lock(&_m);
                    if (_active_fds.count(i) == 0) {
                        printf("Received data from accepted socket %d\n", i);
                        // Read the header.
                        // This helps us dertermine whether client
                        // has closed connection or indeed sending
                        // more bytes to read.
                        CBArg *arg = (CBArg *)malloc(sizeof(CBArg));
                        arg->fd = i;
                        bool is_eof = read_header(i, &arg->hdr);
                        if (is_eof) {
                            printf("Closing connection on socket %d\n", i);
                            free(arg);
                            close(i);
                            FD_CLR(i, &master);
                        } else {
                            // Offload task of intepreting
                            // the opcode and setting/retrieving
                            // data to worker thread.
                            _active_fds.insert(i);
                            _tp.assign_task((void *)arg);
                        }
                    }
                    pthread_mutex_unlock(&_m);
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)

    return 0;
}

void
Memcache::remove_from_active_fds(int fd)
{
    pthread_mutex_lock(&_m);
    _active_fds.erase(fd);
    pthread_mutex_unlock(&_m);
}

bool
Memcache::read_header(int fd, HEADER *hdr)
{
    return read_bytes(fd, (void *)hdr, sizeof *hdr);
}

void
Memcache::execute_opcode(void *arg)
{
    CBArg *cb_arg = (CBArg *)arg;
    int fd = cb_arg->fd;
    HEADER hdr = cb_arg->hdr;
    free(cb_arg);

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
    char *key_buf = (char *)malloc(key_length);
    ASSERT(key_buf, "Failed to allocate key buf");
    memset(key_buf, 0, key_length);

    read_bytes(fd, (void *)key_buf, key_length);
    string key(key_buf, key_length);
    printf("Key: %s\n", key.c_str());

    // Action
    switch (hdr.opcode) {
    case 0x01:  {   // Set
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
    case 0x00:  {   // Get
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
    m.remove_from_active_fds(fd);
    free(key_buf);
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
    m.event_loop();

    return 0;
}
