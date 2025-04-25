#ifndef THREAD_H
#define THREAD_H

#include <csignal>
#include <csetjmp>
#include <memory>

enum class ThreadState { RUNNING, READY, BLOCKED };

typedef void (*thread_entry_point)();

class Thread {
public:
    int id;
    ThreadState state;
    thread_entry_point entry;
    char* stack;
    sigjmp_buf context;
    int total_quantums;

    Thread(int tid, ThreadState state, thread_entry_point entry = nullptr);
    ~Thread();
    void increase_quantums();
    int get_quantums() const;

};

#endif // THREAD_Hgbc