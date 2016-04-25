#include "threadpool.h"
#include <algorithm>
#include "assert_fail.h"

using namespace std;

void
ThreadPool::init(work_cb work_fptr, size_t num_threads)
{
    _do_work = work_fptr;
    _num_threads = max(static_cast<size_t>(1), num_threads);
    int err = pthread_mutex_init(&_m, NULL);
    ASSERT(err == 0, "Failed initializing mutex");

    err = pthread_cond_init(&_cv, NULL);
    ASSERT(err == 0, "Failed initializing condvar");

    _tids.resize(_num_threads);
   for (int i = 0; i < _num_threads; i++) {
        err = pthread_create(&_tids[i], NULL, thread_func, this);
        ASSERT(err == 0, "Failed creating thread");
    }
}

void *
ThreadPool::thread_func(void *arg)
{
    ThreadPool *thread_pool = static_cast<ThreadPool *>(arg);
    thread_pool->serve_loop();
    return NULL;
}

void
ThreadPool::serve_loop(void)
{
    printf("Starting thread %p\n", pthread_self());
    while (true) {
        pthread_mutex_lock(&_m);
        while (_q.empty() && !_shutdown) {
            pthread_cond_wait(&_cv, &_m);
        }
        if (_shutdown) {
            pthread_mutex_unlock(&_m);
            break;
        }
        assert(!_q.empty());
        void *task = _q.front();
         _q.pop_front();
        pthread_mutex_unlock(&_m);

        printf("Thread %p picked task\n",  pthread_self());
        _do_work(task);
    }
    printf("Thread %p exiting\n", pthread_self());
}

void
ThreadPool::assign_task(void *task)
{
    pthread_mutex_lock(&_m);
    if (_shutdown) {
        pthread_mutex_unlock(&_m);
        return;
    }
    _q.push_back(task);
    pthread_cond_signal(&_cv);
    pthread_mutex_unlock(&_m);
}

void
ThreadPool::shutdown(void)
{
    pthread_mutex_lock(&_m);
    _shutdown = true;
    pthread_mutex_unlock(&_m);
    pthread_cond_broadcast(&_cv);

   for (int i = 0; i < _num_threads; i++) {
        pthread_join(_tids[i], NULL);
    }
}

