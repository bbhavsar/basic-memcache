# basic-memcache

Basic memcached server that implements get and set primitives.

Memcached server maintains a thread pool and assigns tasks to
a pool of worker threads. Due to use of thread pool
multiple connections can be serviced simultaneously.

Key-values are maintained in Least Recently Used cache
using LRU eviction policy.

Memcached listens on port 11211

To run the memcached server
```
make
./memcache
```

