//
// Created by einav on 5/14/2025.
//

#include "MapReduceFramework.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>
#include <bitset>
#include <algorithm>
#include "Barrier.h"

constexpr int STATUS_SHIFT = 62;  // bits 62–63
constexpr uint64_t STATUS_MASK = 3LL << STATUS_SHIFT;
constexpr uint64_t TOTAL_MASK = ((1LL<<31)-1) << 31;
struct ThreadContext;
struct Job{
    std::atomic<uint64_t>* job_state;
    std::vector<std::thread>& threads;
    std::vector<ThreadContext>& thread_contexts;
    std::vector<std::vector<IntermediatePair>> shuffled_vec;
    const InputVec& inputVec;
    OutputVec& outputVec;
    const MapReduceClient& client;
    Barrier* barrier;
    std::mutex state_mutex;
    std::mutex join_mutex;
    bool joined = false; // flag if threads were joined

    Job(std::atomic<uint64_t>* job_state, std::vector<std::thread>& threads, std::vector<ThreadContext>& thread_contexts,
        const InputVec& inputVec, OutputVec& outputVec,
        const MapReduceClient& client, Barrier* barrier)
    :
    job_state(job_state),
    threads(threads),
    thread_contexts(thread_contexts),
    inputVec(inputVec),
    outputVec(outputVec),
    client(client),
    barrier(barrier)
    {
    }

    ~Job(){
        delete job_state;
        delete barrier;
        delete &threads;
        delete &thread_contexts;
    }
};




struct ThreadContext{
    int id ;
    IntermediateVec intermediate_vec;
    Job * assigned_job = nullptr;

};


void mapPhase(ThreadContext * threadContext );
void sortPhase(ThreadContext * threadContext);
void shufflePhase(Job* job);
void reducePhase(ThreadContext * threadContext);


JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec,
                            int multiThreadLevel){
    auto* threads = new std::vector<std::thread>();
    threads->reserve(multiThreadLevel);
    auto* contexts = new std::vector<ThreadContext>(multiThreadLevel);
    auto* job_state = new std::atomic<uint64_t>();
    size_t input_size = inputVec.size();

// Shift left by 32 to place it in bits 62–32
    uint64_t encoded = static_cast<uint64_t>(input_size) << 31;

// Store in job_state
    job_state->store(encoded);

    Barrier* barrier = new Barrier(multiThreadLevel);

    Job* job = new Job(job_state, *threads, *contexts, inputVec, outputVec, client, barrier);
    for (int i = 0; i < multiThreadLevel; ++i) {
        (*contexts)[i].id = i;
        (*contexts)[i].assigned_job = job;
        threads->emplace_back(&mapPhase, &(*contexts)[i]); // the thread created starts running
    }
    return static_cast<JobHandle>(job);  // Cast pointer to void*

}

void waitForJob(JobHandle job){
    Job* casted_job = static_cast<Job*>(job);
    std::lock_guard<std::mutex> lock(casted_job->join_mutex); // automaticly lock
    if (!casted_job->joined){
        for (auto& t : casted_job->threads) {
            t.join(); // end the loop only when all threads finish
        }
        casted_job->joined = true;
    }

}

std::string to_binary(uint64_t value) {
    return std::bitset<64>(value).to_string();
}

void getJobState(JobHandle job, JobState* state){
    Job* casted_job = static_cast<Job*>(job);
    std::lock_guard<std::mutex> lock(casted_job->join_mutex); // automaticly lock
    int64_t monitor = casted_job->job_state->load();
    stage_t current_stage = static_cast<stage_t>((monitor & STATUS_MASK) >> STATUS_SHIFT);
    uint64_t processed = monitor & 0X7FFFFFFF;
    uint64_t total = (monitor & TOTAL_MASK) >> 31;
    state -> percentage = (processed / (float ) total) * 100;
    state -> stage = current_stage;

}

void closeJobHandle(JobHandle job){
    Job* casted_job = static_cast<Job*>(job);
    waitForJob(job);
    delete casted_job;
    job = nullptr;
}



void changeState(stage_t cur_state, ThreadContext* threadContext, uint64_t new_total){
    std::lock_guard<std::mutex> lock(threadContext->assigned_job->state_mutex); // automaticly lock
    std::atomic<uint64_t>* job_state = threadContext ->assigned_job->job_state;
    uint64_t state = job_state -> load();
    // for stage flag
    stage_t current_status = static_cast<stage_t>((state & STATUS_MASK) >> STATUS_SHIFT);
    if (current_status == cur_state){
        // set state
        uint64_t new_state = new_total << 31;
        new_state += (cur_state+1ULL) << STATUS_SHIFT;
        threadContext ->assigned_job->job_state-> store(new_state);

    }
}

void emit2 (K2* key, V2* value, void* context){
    ThreadContext* tc = static_cast<ThreadContext*>(context);
    tc->intermediate_vec.push_back(IntermediatePair(key, value));
}

void emit3 (K3* key, V3* value, void* context){
    ThreadContext* tc = static_cast<ThreadContext*>(context);
    std::lock_guard<std::mutex> lock(tc->assigned_job->state_mutex); // automaticly lock
    tc->assigned_job->outputVec.push_back(OutputPair (key, value));
}


//void mapPhase(ThreadContext * threadContext){
//    changeState(UNDEFINED_STAGE, threadContext);
//    JobState* js= new JobState;
//    getJobState(threadContext->assigned_job, js);
//    while (js -> percentage < 100){
//        int64_t monitor = threadContext->assigned_job->job_state->load();
//        int cur_index = monitor & 0X7FFFFFFF;
//        InputPair pair = threadContext->assigned_job->inputVec[cur_index];
//        threadContext->assigned_job->client.map(pair.first, pair.second, threadContext);
//        getJobState(threadContext->assigned_job, js);
//    }
//    sortPhase(threadContext);
//}


void mapPhase(ThreadContext * threadContext){
    changeState(UNDEFINED_STAGE, threadContext, threadContext -> assigned_job -> inputVec.size());
    const InputVec& inputVec = threadContext->assigned_job->inputVec;
    uint64_t old_state = threadContext->assigned_job->job_state->fetch_add(1);
    uint64_t old_value = old_state & 0X7FFFFFFF;
    while (old_value < inputVec.size()){
        InputPair pair = threadContext->assigned_job->inputVec[old_value];
        threadContext->assigned_job->client.map(pair.first, pair.second, threadContext);
        old_state = threadContext->assigned_job->job_state->fetch_add(1);
        old_value = old_state & 0X7FFFFFFF;

    }
    if (old_value >= inputVec.size()){
        old_state = threadContext->assigned_job->job_state->fetch_add(-1);
    }
    sortPhase(threadContext);
}

void sortPhase(ThreadContext * threadContext){
    IntermediateVec inter_vec = threadContext->intermediate_vec;
    std::sort(inter_vec.begin(), inter_vec.end(), [](IntermediatePair& a, IntermediatePair& b){
        return a.first < b.first;
    });
//    fprintf(stdout, "%d at barrier 1\n", threadContext -> id);
    threadContext -> assigned_job -> barrier -> barrier();
    if (threadContext -> id == 0){
        unsigned long cnt = 0;
        for (ThreadContext& tc: threadContext->assigned_job->thread_contexts){
            cnt += tc.intermediate_vec.size();
        }
        changeState(MAP_STAGE, threadContext, cnt);
        shufflePhase(threadContext -> assigned_job);
    }
//    fprintf(stdout, "%d at barrier 2\n", threadContext -> id);
    threadContext -> assigned_job -> barrier -> barrier();
    reducePhase(threadContext);
}

K2* getMaxKey(Job* job) {
    K2* max = nullptr;
    for (ThreadContext& tc: job->thread_contexts) {
        IntermediateVec& vec = tc.intermediate_vec;
        if (!vec.empty()) {
            K2* candidate = vec.back().first;
            if (max == nullptr || *max < *candidate) {
                max = candidate;
            }
        }
    }
    return max;
}

static bool K2s_equal(K2* a, K2* b) {
    return !(*a < *b) && !(*b < *a);
}


void shufflePhase(Job* job){
    while (true) {
        K2* current = getMaxKey(job);
        if (current == nullptr) {
            break;
        }

        IntermediateVec to_reduce;

        for (ThreadContext& ct : job -> thread_contexts) {
            IntermediateVec& vec = ct.intermediate_vec;
            while (!vec.empty() && K2s_equal(current, vec.back().first)) {
                to_reduce.push_back(vec.back());
                vec.pop_back();

                // Update atomic progress if needed
                job->job_state->fetch_add(1);
            }
        }

        job->shuffled_vec.push_back(to_reduce);
    }
}


void reducePhase(ThreadContext * threadContext){
    changeState(SHUFFLE_STAGE, threadContext, threadContext->assigned_job->shuffled_vec.size());
    uint64_t old_state = threadContext->assigned_job->job_state->fetch_add(1);
    uint64_t old_value = old_state & 0X7FFFFFFF;
    while (old_value < threadContext->assigned_job->shuffled_vec.size()){ // should we use atomic value instead of size?????????????????????

//        std::vector<IntermediatePair> vec = threadContext->assigned_job->shuffled_vec[old_value];
        threadContext->assigned_job->client.reduce(&threadContext->assigned_job->shuffled_vec[old_value], threadContext);
        old_state = threadContext->assigned_job->job_state->fetch_add(1);
        old_value = old_state & 0X7FFFFFFF;

    }
    if (old_value >= threadContext->assigned_job->shuffled_vec.size()){
        old_state = threadContext->assigned_job->job_state->fetch_add(-1);
    }
}







