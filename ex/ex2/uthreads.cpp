#include <csignal>
#include <sys/time.h>

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <string>
#include "uthreads.h"
#include "thread.h"

#include <cassert>
#include <iostream>
#include <queue>
#include <stdbool.h>
#include <unordered_map>


#define SYSTEM_ERROR_MSG "system error: "
#define LIBRARY_ERROR_MSG "thread library error: "
#define SYSTEM_ERROR_IND 0
#define LIBRARY_ERROR_IND 1
#define SECOND 1000000

#define TIMER_ON sigprocmask(SIG_UNBLOCK, &timer_sigset, NULL);
#define TIMER_OFF sigprocmask(SIG_BLOCK, &timer_sigset, NULL);






static struct itimerval timer;

// ID-to-thread mapping
static std::unordered_map<int, Thread*> thread_map;

// Ready queue for Round-Robin
static std::queue<int> ready_queue;

// Min-heap of free thread IDs
static std::priority_queue<int, std::vector<int>, std::greater<>> free_tids;

// map for sleeping threads

static std::unordered_map<int, int> sleeping_map;

// queue for awakening threads from sleep

static std::queue<int> wake_queue;

int running_tid;
static int total_quantums;

// signal set for timer
sigset_t timer_sigset;


void free_resources() {
    for (auto it : thread_map) {
        delete it.second;
    }
    thread_map.clear();
}

/*
 *ERROR_TYPE: 0 if system error, 1 else
 *
 */
void error_handler(std::string msg, int error_type) {
    if (error_type == SYSTEM_ERROR_IND) {
        std::cerr << SYSTEM_ERROR_MSG << msg << std::endl;
        free_resources();
        exit(1);
    }
    std::cerr << LIBRARY_ERROR_MSG << msg << std::endl;
}

void move_to_next(bool should_delete_thread=false) {
  	//save current state of running thread
    if (sigsetjmp(thread_map[running_tid]->context, 1)==0){
        if (should_delete_thread) {
            delete thread_map[running_tid];
            thread_map.erase(running_tid);
        }
        Thread* next_thread = thread_map[ready_queue.front()];
        ready_queue.pop();
        next_thread -> state = ThreadState::RUNNING;
        running_tid = next_thread -> id;
        thread_map[running_tid] -> increase_quantums();
        siglongjmp(next_thread -> context, 1);
    }


}

void reset_timer() {
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr))
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr))
    {
        error_handler("problem setting timer", SYSTEM_ERROR_IND);
        free_resources();
        exit(1);
    }
    total_quantums++;
}

void timer_handler(int sig) {
    TIMER_OFF
    // Step 1: Wake sleeping threads whose timers expired
    std::vector<int> expired;
    for (auto& [tid, counter] : sleeping_map) {
        if (--sleeping_map[tid] == 0) {
            expired.push_back(tid);
        }
    }
    for (int tid : expired) {
        sleeping_map.erase(tid);
        if (!thread_map[tid]->is_blocked) {
            wake_queue.push(tid);
        }
    }

    // Step 2: Move waking threads to READY state
    while (!wake_queue.empty()) {
        int tid = wake_queue.front();
        wake_queue.pop();
        Thread* thread = thread_map[tid];
        thread->state = ThreadState::READY;
        ready_queue.push(tid);
    }

    // Step 3: Context switch if needed
    if (!ready_queue.empty()) {
        // Switch to next thread
        thread_map[running_tid] -> state = ThreadState::READY;
        ready_queue.push(running_tid);
        TIMER_ON
        move_to_next();
    } else {
        thread_map[running_tid] -> increase_quantums();
    }
    total_quantums++;
    TIMER_ON
}





void timer_init(int quantum_usecs) {
    struct sigaction sa = {0};
    sigemptyset(&timer_sigset);
    sigaddset(&timer_sigset, SIGVTALRM);


    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        error_handler("sigaction failed", SYSTEM_ERROR_IND);
        free_resources();
        exit(1);
    }

    // Configure the timer to expire after 1 sec... */
    timer.it_value.tv_sec = quantum_usecs / SECOND;        // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs % SECOND;        // first time interval, microseconds part

    // configure the timer to expire every 3 sec after that.
    timer.it_interval.tv_sec = quantum_usecs / SECOND;    // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs % SECOND;    // following time intervals, microseconds part

    reset_timer();


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
    if (quantum_usecs <= 0) {
        error_handler("quantum_usecs must be positive", SYSTEM_ERROR_IND);
        return -1;
    }


    auto* main_thread = new Thread(get_tid(), ThreadState::RUNNING, nullptr);
    thread_map[main_thread->id] = main_thread;
    running_tid = main_thread->id;
    main_thread -> increase_quantums();
    // Now save the real environment of the main thread
    if (sigsetjmp(main_thread->context, 1) != 0) {
        // If we return here from siglongjmp, just return
        return 0;
    }

    timer_init(quantum_usecs);

    return 0;
}




int uthread_spawn(thread_entry_point entry_point) {
    Thread* thread = nullptr;
    if (entry_point == nullptr) {
        error_handler("entry_point is null", LIBRARY_ERROR_IND);
        return -1;
    }
    if (thread_map.size() == MAX_THREAD_NUM) {
        error_handler("thread_map is full", LIBRARY_ERROR_IND);
        return -1;
    }
    try {
        TIMER_OFF

        thread = new Thread(get_tid(), ThreadState::READY, entry_point);
        thread_map[thread -> id] = thread;
        ready_queue.push(thread -> id);
        TIMER_ON

        return thread->id;
    } catch (const std::exception& e) {
        error_handler(e.what(), SYSTEM_ERROR_IND);
        return -1;
    }
}

void remove_from_ready_queue(int tid_to_remove) {
    std::queue<int> temp;
    while (!ready_queue.empty()) {
        int tid = ready_queue.front();
        ready_queue.pop();
        if (tid != tid_to_remove) {
            temp.push(tid);
        }
    }
    ready_queue = std::move(temp);
}


bool tid_exists(int tid) {
    if (thread_map.find(tid) == thread_map.end()) {
        error_handler("tid not found", LIBRARY_ERROR_IND);
        return false;
    }
    return true;
}


int uthread_terminate(int tid) {
    TIMER_OFF
    if (!tid_exists(tid)) {
        return -1;
    }
    if (tid==0) {
        free_resources();
        exit(0);
    }
    Thread* thread = thread_map[tid];
    if (thread->state == ThreadState::READY) {
        remove_from_ready_queue(thread->id);
    }


    sleeping_map.erase(tid);
    free_tids.push(tid);

    if (thread_map[tid] -> state == ThreadState::RUNNING) {
        TIMER_ON
        reset_timer();
        move_to_next(true);

    }
    TIMER_ON
    return 0;
}

int uthread_block(int tid) {
    TIMER_OFF
    if (!tid_exists(tid)) {
        return -1;
    }
    if (tid == 0) {
        error_handler("cannot block main thread", LIBRARY_ERROR_IND);
        return -1;
    }
    if (thread_map[tid] -> state == ThreadState::READY) {
        remove_from_ready_queue(tid);
    }
    thread_map[tid] -> state = ThreadState::BLOCKED;
    thread_map[tid] -> is_blocked = true;
    if (running_tid == tid) {
        TIMER_ON
        reset_timer();
        move_to_next();
    }
    TIMER_ON
    return 0;
}

int uthread_resume(int tid) {
    TIMER_OFF

    if (!tid_exists(tid)) {
        return -1;
    }
    if (thread_map[tid] -> is_blocked == true) {
        thread_map[tid] -> is_blocked = false;
        if (sleeping_map.find(tid) == sleeping_map.end()) {
            thread_map[tid] -> state = ThreadState::READY;
            ready_queue.push(tid);
        }
    }
    TIMER_ON

    return 0;
}

int uthread_sleep(int num_quantums) {
    TIMER_OFF
    if (running_tid == 0) {
        error_handler("can't block main thread", LIBRARY_ERROR_IND);
        return -1;
    }
    sleeping_map[running_tid] = num_quantums;
    thread_map[running_tid] -> state = ThreadState::BLOCKED;
    TIMER_ON
    reset_timer();
    move_to_next();
    return 0;
}

int uthread_get_tid() {
    return running_tid;
}


int uthread_get_total_quantums() {
    return total_quantums;

}

int uthread_get_quantums(int tid) {
    if (!tid_exists(tid)) {
        return -1;
    }
    return thread_map[tid]->get_quantums();
}




