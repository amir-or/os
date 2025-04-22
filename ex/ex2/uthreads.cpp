#include <csignal>
#include <sys/time.h>

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <string>
#include "uthreads.h"

#include <cassert>
#include <iostream>
#include <queue>
#include <stdbool.h>
#include <unordered_map>

#define SYSTEM_ERROR_MSG "system error: "
#define LIBRARY_ERROR_MSG "thread library error: "
#define SYSTEM_ERROR_IND 0
#define LIBRARY_ERROR_IND 1

typedef enum { RUNNING, READY, BLOCKED } ThreadState;


typedef struct {
    int id;
    ThreadState state;
    thread_entry_point entry;
    char* stack;
    sigjmp_buf context;
} Thread;


static struct itimerval timer;
int cur_tid;
// ID-to-thread mapping
static std::unordered_map<int, Thread*> thread_map;

// Ready queue for Round-Robin
static std::queue<int> ready_queue;

// Min-heap of free thread IDs
static std::priority_queue<int, std::vector<int>, std::greater<int>> free_tids;

int running_tid;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
        "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif

/*
 *ERROR_TYPE: 0 if system error, 1 else
 *
 */
void error_handler(std::string msg, int ERROR_TYPE) {
    if (ERROR_TYPE == SYSTEM_ERROR_IND) {
        std::cerr << SYSTEM_ERROR_MSG << msg << std::endl;
        exit(1);
    }
    std::cerr << LIBRARY_ERROR_MSG << msg << std::endl;

}

void timer_handler(int sig) {
    if (ready_queue.size() > 1) {

        Thread* thread = thread_map[running_tid];
        assert(thread -> state == RUNNING);
        thread->state = READY;
        // add to the end of the queue
        ready_queue.push(running_tid);

        //change the state of the current first in queue thread and pop it
        Thread* next_thread = thread_map[ready_queue.front()];
        ready_queue.pop();
        next_thread -> state = RUNNING;
        running_tid = next_thread -> id;

    }
}


bool timer_init(int quantum_usecs) {
    struct sigaction sa = {0};


    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        error_handler("sigaction failed", SYSTEM_ERROR_IND);
    }

    // Configure the timer to expire after 1 sec... */
    timer.it_value.tv_sec = 0;        // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs;        // first time interval, microseconds part

    // configure the timer to expire every 3 sec after that.
    timer.it_interval.tv_sec = 0;    // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs;    // following time intervals, microseconds part

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        error_handler("problem setting timer", SYSTEM_ERROR_IND);
        return false;
    }
    return true;
}

int get_tid() {
    int tid=-1;
    if (!free_tids.empty()) {
        tid = free_tids.top();
        free_tids.pop();
    } else if (thread_map.size() < MAX_THREAD_NUM) {
        tid = thread_map.size(); // or keep a running counter
    }
    return tid;
}

int uthread_init(int quantum_usecs) {
    timer_init(quantum_usecs);

    auto* main_thread = new Thread;



    main_thread->id = get_tid();
    main_thread->state = RUNNING;
    main_thread->entry = nullptr;
    main_thread->stack = nullptr;
    thread_map[main_thread->id] = main_thread;
    running_tid = main_thread->id;

    return 0;
}


