#include "Barrier.h"

Barrier::Barrier(int numThreads)
        : count(0)
        , generation(0)
        , numThreads(numThreads)
{ }


void Barrier::barrier() { // wait until everyone comes
    std::unique_lock<std::mutex> lock(mutex);
    int gen = generation;

    if (++count < numThreads) {
        cv.wait(lock, [this, gen] { return gen != generation; }); // everyone waits until gen != generation
    } else { // when the last thread comes
        count = 0;
        generation++;
        cv.notify_all(); // it notifies the rest to check whether they can wake up
    }
}
