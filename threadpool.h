/*
 * Interface for thread pool
 */

#include <iostream>
#include <pthread.h>
#include <vector>
#include <list>

using namespace std;

typedef void (*work_cb)(void *arg);

class ThreadPool
{
public:
    ThreadPool() : _num_threads(0), _shutdown(false)
    { }
    void init(work_cb work_fptr, size_t num_threads = 4);
    void assign_task(void *task);
    void shutdown(void);

    static void* thread_func(void *arg);

private:
    void serve_loop(void);

private:
    size_t              _num_threads;
    // Whether threadpool was requested to shutdown
    bool                _shutdown;
    vector<pthread_t>   _tids;
    // Queue of pending tasks available to be picked up
    list<void *>        _q;
    // Synchronization primitives to serialize access
    // to private data member and signal thread to wakeup
    // on availability of new tasks.
    pthread_mutex_t     _m;
    pthread_cond_t      _cv;
    // Function invoked by worker thread to carry out task
    work_cb             _do_work;
};

