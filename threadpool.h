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
    {
    }
    void init(work_cb work_fptr, size_t num_threads = 4);
    void assign_task(void *task);
    void shutdown(void);

    static void* thread_func(void *arg);

private:
    void serve_loop(void);

private:
    size_t _num_threads;
    bool _shutdown;
    vector<pthread_t> _tids;
    list<void *> _q;
    pthread_mutex_t _m;
    pthread_cond_t _cv;
    work_cb _do_work;
};

