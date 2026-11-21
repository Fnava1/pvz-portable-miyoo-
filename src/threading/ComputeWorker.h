#pragma once

#include <atomic>
#include <cstdint>

#if defined(_WIN32) && !__has_include(<pthread.h>)
typedef void* pthread_t;
typedef void* pthread_mutex_t;
typedef void* pthread_cond_t;
#define PTHREAD_MUTEX_INITIALIZER nullptr
#define PTHREAD_COND_INITIALIZER nullptr
#else
#include <pthread.h>
#endif

enum WorkerTask : uint32_t {
    WORKER_TASK_IDLE = 0,
    WORKER_TASK_EVAL_RIGS,
    WORKER_TASK_DOWNSCALE,
    WORKER_TASK_SHUTDOWN
};

struct alignas(64) WorkerCommand {
    std::atomic<uint32_t> mTask{WORKER_TASK_IDLE};
    const void*           mSrcBuffer{nullptr};
    void*                 mDstBuffer{nullptr};
    int                   mParam1{0}; // Start index or width
    int                   mParam2{0}; // Count or height
    std::atomic<uint32_t> mCompleted{1};
};

class ComputeWorker {
public:
    static void Init();
    static void Shutdown();
    static void DispatchTask(WorkerTask task, const void* src, void* dst, int p1, int p2);
    static void WaitForTask();
    static bool IsBusy();

private:
    static void* WorkerThreadEntry(void* arg);
    static void  ExecuteDownscale(const void* src, void* dst, int srcW = 800, int srcH = 600);
    static void  ExecuteRigEvaluation(const void* src, int startIndex, int count);

    static pthread_t         sThread;
    static WorkerCommand     sCommand;
    static pthread_mutex_t   sMutex;
    static pthread_cond_t    sCond;
    static std::atomic<bool> sRunning;
};
