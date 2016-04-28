/*
 * Least Recently Used (LRU) cache interface.
 */

#include <iostream>
#include <string>
#include <list>
#include <map>

#include <pthread.h>
#include "assert_fail.h"

using namespace std;

class LRUCache
{
public:
    LRUCache(size_t capacity_bytes) :
         _capacity_bytes(capacity_bytes),
        _usage_bytes(0)
    {
        int err = pthread_mutex_init(&_m, NULL);
        ASSERT(err == 0, "Failed initializing mutex");
    }

    // Fetch value associated with the key.
    // Return the value in 'val' and size of the value in 'num_bytes' as output params.
    // Returns true if key is present in the cache.
    bool get(const std::string& key, void **val, size_t *val_bytes, void **extra, size_t *extra_bytes);
    // Set value in the cache.
    void set(const std::string& key, void *val, size_t val_bytes, void *extra, size_t extra_bytes);
private:
    // Evict least recently key-value from the back of the queue.
    void evict(void);

    // Inner class stored in the queue containing
    // key and memory ptr to value.
    struct KeyValue {
        string key;
        void *val;
        size_t val_bytes; // size of value
        void *extra;
        size_t extra_bytes;
    };
    typedef map<string, list<KeyValue>::iterator > LookupMap;

    // Queue maintaing the LRU order.
    // Most recently used key-val is in front and least recently
    // used at the back.
    list<KeyValue>  _q;
    // Map providing efficient (log n) lookup to element in the queue
    // on get/set.
    LookupMap       _lookup_map;
    const size_t    _capacity_bytes;
    size_t          _usage_bytes;
    // Lock synchronizing access the LRU internal data members.
    pthread_mutex_t _m;
};


