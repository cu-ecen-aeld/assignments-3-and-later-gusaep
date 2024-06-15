#include "threading.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg, ...) printf("threading ERROR: " msg "\n", ##__VA_ARGS__)

void *threadfunc(void *thread_param)
{

    // wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int rem = usleep(thread_func_args->wait_to_obtain_ms * 1000);
    if (rem != 0) {
        ERROR_LOG("Sleep interrupted");
    }

    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0) {
        ERROR_LOG("Failed to lock mutex: %d", rc);
    }

    rem = usleep(thread_func_args->wait_to_release_ms * 1000);
    if (rem != 0) {
        ERROR_LOG("Sleep interrupted");
    }
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0) {
        ERROR_LOG("Failed to unlock mutex: %d", rc);
    }

    thread_func_args->thread_complete_success = true;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,
                                  int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *th_data = (struct thread_data*)calloc(1, sizeof(struct thread_data));
    if (th_data == NULL)
    {
        ERROR_LOG("Struct allocation failed");
        return false;
    }
    th_data->mutex = mutex;
    th_data->wait_to_obtain_ms = wait_to_obtain_ms;
    th_data->wait_to_release_ms = wait_to_release_ms;
    th_data->thread_complete_success = false;
    
    int rc = pthread_create(thread, NULL, threadfunc, th_data);
    if (rc != 0) {
        ERROR_LOG("Failed create a new thread");
        return false;
    }

    return true;
}
