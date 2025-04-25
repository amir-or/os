#include "thread.h"
#include <cstdlib>
#include <cstring>
#include <cassert>

#include "uthreads.h"

#ifdef __x86_64__
typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}
#else
typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}
#endif

Thread::Thread(int tid, ThreadState state, thread_entry_point entry)
    : id(tid), state(state), entry(entry), stack(nullptr)
{
    total_quantums = 0;

    if (tid != 0) {
        // Regular (spawned) thread
        stack = new char[STACK_SIZE];
        address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t)entry;
        sigsetjmp(context, 1);

        // Set stack pointer and program counter manually
        context->__jmpbuf[JB_SP] = translate_address(sp);
        context->__jmpbuf[JB_PC] = translate_address(pc);
        sigemptyset(&context->__saved_mask);  // Also reset signal mask
    }
    is_blocked = false;
}



Thread::~Thread() {
    if (stack != nullptr) {
        delete[] stack;
    }
}

void Thread::increase_quantums() {
    total_quantums++;
}

int Thread::get_quantums() const {
    return total_quantums;
}


