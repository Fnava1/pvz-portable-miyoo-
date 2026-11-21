#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ComputeWorker.h"

#include <atomic>
#include <cstdint>
#include <ctime>

#ifndef _WIN32
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#endif

// Forward declaration of Reanimation and ReanimRigEvaluator batch evaluation
class Reanimation;

#if __has_include("PvzpLib/ReanimRigEvaluator.h")
#include "PvzpLib/ReanimRigEvaluator.h"
#endif

namespace ReanimRigEvaluator {
    void EvaluateBatch(Reanimation** theRigs, int startIndex, int count);
}

// Forward declaration of external NEON downscaler (defined in GLInterface.cpp)
extern void NeonBilinearDownscale5to4_RGB565(const uint32_t* src, uint16_t* dst, int srcW, int srcH, int dstW, int dstH);

pthread_t         ComputeWorker::sThread{};
WorkerCommand     ComputeWorker::sCommand{};
pthread_mutex_t   ComputeWorker::sMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t    ComputeWorker::sCond = PTHREAD_COND_INITIALIZER;
std::atomic<bool> ComputeWorker::sRunning{false};

void ComputeWorker::Init() {
    if (sRunning.load(std::memory_order_relaxed)) return;
    sRunning.store(true, std::memory_order_release);
    sCommand.mTask.store(WORKER_TASK_IDLE, std::memory_order_release);
    sCommand.mCompleted.store(1, std::memory_order_release);

#ifndef _WIN32
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024); // 256 KB stack
    pthread_create(&sThread, &attr, WorkerThreadEntry, nullptr);
    pthread_attr_destroy(&attr);

    // Pin compute worker thread to CPU 1
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_setaffinity_np(sThread, sizeof(cpu_set_t), &cpuset);
#endif
}

void* ComputeWorker::WorkerThreadEntry(void*) {
#ifndef _WIN32
    // Ensure CPU 1 affinity within the thread itself
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

    while (sRunning.load(std::memory_order_acquire)) {
        // Tier 1: Intra-frame spin-yield with sched_yield() up to 500 µs
        bool taskFound = false;

#ifndef _WIN32
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);

        while (true) {
            uint32_t task = sCommand.mTask.load(std::memory_order_acquire);
            if (task != WORKER_TASK_IDLE) {
                taskFound = true;
                break;
            }
            sched_yield();
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsedUs = (now.tv_sec - start.tv_sec) * 1000000L + (now.tv_nsec - start.tv_nsec) / 1000L;
            if (elapsedUs >= 500L) {
                break;
            }
        }
#endif

        // Tier 2: Low-power dormant WFI wait
        if (!taskFound) {
#ifndef _WIN32
            pthread_mutex_lock(&sMutex);
            while (sCommand.mTask.load(std::memory_order_acquire) == WORKER_TASK_IDLE &&
                   sRunning.load(std::memory_order_relaxed)) {
                pthread_cond_wait(&sCond, &sMutex);
            }
            pthread_mutex_unlock(&sMutex);
#endif
        }

        uint32_t task = sCommand.mTask.load(std::memory_order_acquire);
        if (task == WORKER_TASK_SHUTDOWN || !sRunning.load(std::memory_order_relaxed)) {
            break;
        }

        if (task == WORKER_TASK_DOWNSCALE) {
            ExecuteDownscale(sCommand.mSrcBuffer, sCommand.mDstBuffer, sCommand.mParam1, sCommand.mParam2);
        } else if (task == WORKER_TASK_EVAL_RIGS) {
            ExecuteRigEvaluation(sCommand.mSrcBuffer, sCommand.mParam1, sCommand.mParam2);
        }

        sCommand.mTask.store(WORKER_TASK_IDLE, std::memory_order_release);
        sCommand.mCompleted.store(1, std::memory_order_release);
    }
    return nullptr;
}

void ComputeWorker::ExecuteDownscale(const void* src, void* dst, int srcW, int srcH) {
    if (!src || !dst) return;
    int w = (srcW > 0) ? srcW : 800;
    int h = (srcH > 0) ? srcH : 600;
    NeonBilinearDownscale5to4_RGB565(reinterpret_cast<const uint32_t*>(src),
                                     reinterpret_cast<uint16_t*>(dst),
                                     w, h, 640, 480);
}

void ComputeWorker::ExecuteRigEvaluation(const void* src, int startIndex, int count) {
    if (!src || count <= 0) return;
    Reanimation** rigs = const_cast<Reanimation**>(reinterpret_cast<Reanimation* const*>(src));
    ReanimRigEvaluator::EvaluateBatch(rigs, startIndex, count);
}

void ComputeWorker::DispatchTask(WorkerTask task, const void* src, void* dst, int p1, int p2) {
    if (sCommand.mCompleted.load(std::memory_order_acquire) == 0) {
        WaitForTask();
    }
    sCommand.mCompleted.store(0, std::memory_order_release);
    sCommand.mSrcBuffer = src;
    sCommand.mDstBuffer = dst;
    sCommand.mParam1 = p1;
    sCommand.mParam2 = p2;
#ifndef _WIN32
    pthread_mutex_lock(&sMutex);
    sCommand.mTask.store(task, std::memory_order_release);
    pthread_cond_signal(&sCond);
    pthread_mutex_unlock(&sMutex);
#else
    sCommand.mTask.store(task, std::memory_order_release);
#endif
}

void ComputeWorker::WaitForTask() {
    while (sCommand.mCompleted.load(std::memory_order_acquire) == 0) {
#ifndef _WIN32
        sched_yield();
#endif
    }
}

bool ComputeWorker::IsBusy() {
    return sCommand.mCompleted.load(std::memory_order_acquire) == 0;
}

void ComputeWorker::Shutdown() {
    if (!sRunning.load(std::memory_order_relaxed)) return;
    sRunning.store(false, std::memory_order_release);
#ifndef _WIN32
    pthread_mutex_lock(&sMutex);
    sCommand.mTask.store(WORKER_TASK_SHUTDOWN, std::memory_order_release);
    pthread_cond_signal(&sCond);
    pthread_mutex_unlock(&sMutex);
    pthread_join(sThread, nullptr);
#endif
}
