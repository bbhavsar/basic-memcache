#include <iostream>
#include "threadpool.h"
#include "lrucache.h"

using namespace std;

static const char *PORT = "11211";
static const int BACKLOG = 10;

class Memcache
{
public:
    Memcache(size_t capacity_bytes) : _c(capacity_bytes)
    {   }
    void init(void)
    {
        _tp.init(read_socket);
    }

    int accept_loop(void);
    static void read_socket(void *arg);
private:
    void respond_to_set(int fd);
    void respond_to_get(int fd, bool available, void *val, size_t len);
private:
    LRUCache _c;
    ThreadPool _tp;
};

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


