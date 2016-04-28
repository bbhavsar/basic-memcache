#include "lrucache.h"
#include "assert_fail.h"
#include <stdlib.h>
#include <string.h>

bool
LRUCache::get(const std::string& key, void **val, size_t *val_bytes,
              void **extra, size_t *extra_bytes)
{
    pthread_mutex_lock(&_m);
    LookupMap::iterator map_it = _lookup_map.find(key);
    if (map_it == _lookup_map.end()) {
        *val = NULL;
        *val_bytes = 0;
        *extra = NULL;
        *extra_bytes = 0;
        pthread_mutex_unlock(&_m);
        return false;
    }

    // Key is present in the cache.
    // Move key and associated meta-data to front
    // of the queue and adjust ptrs.
    list<KeyValue>::iterator list_it = map_it->second;
    KeyValue kv = *list_it;
    assert(kv.key == key);
    _q.erase(list_it);
    _q.push_front(kv);
    _lookup_map[key] = _q.begin();

    *val = kv.val;
    *val_bytes = kv.val_bytes;
    *extra = kv.extra;
    *extra_bytes = kv.extra_bytes;

    pthread_mutex_unlock(&_m);
    return true;
}

void
LRUCache::evict(void)
{
    assert(!_q.empty());
    KeyValue kv = _q.back();
    printf("Evicting key %s\n", kv.key.c_str());
    _lookup_map.erase(kv.key);
    _usage_bytes -= kv.val_bytes;
    _usage_bytes -= kv.extra_bytes;
    free(kv.val);
    free(kv.extra);
    _q.pop_back();
}

void
LRUCache::set(const std::string& key, void *val, size_t val_bytes,
              void *extra, size_t extra_bytes)
{
    pthread_mutex_lock(&_m);
    assert(val_bytes + extra_bytes <= _capacity_bytes);

    // Key may already be present in the cache
    // and it's being overwritten/updated.
    LookupMap::iterator map_it = _lookup_map.find(key);
    if (map_it != _lookup_map.end()) {
        list<KeyValue>::iterator list_it = map_it->second;
        KeyValue old_kv = *list_it;
        assert(old_kv.key == key);
        free(old_kv.val);
        free(old_kv.extra);
        _usage_bytes -= old_kv.val_bytes;
        _usage_bytes -= old_kv.extra_bytes;
        _q.erase(list_it);
        // _lookup_map will be updated below
    }

    // Evict least recently used buffers till space is available.
    while ((_usage_bytes + val_bytes + extra_bytes) > _capacity_bytes) {
        evict();
    }

    KeyValue kv;
    kv.key = key;
    kv.val_bytes = val_bytes;
    kv.val = malloc(val_bytes);
    ASSERT(kv.val != NULL, "Failed allocating memory for value");
    memcpy(kv.val, val, val_bytes);
    _usage_bytes += val_bytes;

    kv.extra_bytes = extra_bytes;
    kv.extra = malloc(extra_bytes);
    ASSERT(kv.extra != NULL, "Failed allocating memory for extra length");
    memcpy(kv.extra, extra, extra_bytes);
    _usage_bytes += extra_bytes;

    _q.push_front(kv);
    _lookup_map[key] = _q.begin();

    pthread_mutex_unlock(&_m);
}

