#include <iostream>
#include <set>
#include "threadpool.h"
#include "lrucache.h"

using namespace std;

// Memache protocol header
typedef struct __attribute__((__packed__)) {
    uint8_t magic;
    uint8_t opcode;
    uint16_t key_length;

    uint8_t extras_length;
    uint8_t data_type;
    union {
        uint16_t reserved;
        uint16_t status;
    };

    uint32_t total_body_length;
    uint32_t opaque;
    uint64_t cas;
} HEADER;

class Memcache
{
public:
    Memcache(size_t capacity_bytes) : _c(capacity_bytes)
    {   }
    void init(void)
    {
        _tp.init(execute_opcode);
        pthread_mutex_init(&_m, NULL);
    }

    // Main event loop
    int event_loop(void);
    // Callback function invoked by worker thread
    // in threadpool to take necessary action.
    static void execute_opcode(void *arg);
private:
    void respond_to_set(int fd);
    void respond_to_get(int fd, bool available, void *val, size_t len);
    void remove_from_active_fds(int fd);

    // Returns whether EOF
    bool read_header(int fd, HEADER *hdr);
private:
    struct CBArg {
        HEADER hdr;
        int fd;
    };
    LRUCache        _c;
    ThreadPool      _tp;
    pthread_mutex_t _m;
    // Set of active fds indicating whether worker thread
    // is working on a file descriptor.
    set<int>        _active_fds;
};

