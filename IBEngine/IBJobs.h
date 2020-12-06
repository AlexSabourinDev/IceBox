#pragma once

#include "IBEngineAPI.h"
#include <stdint.h>
#include <string.h>

/*
## Why do we want a job system?
A job system is valuable because it allows us to dispatch multi-threaded workloads
without having to continuously spawn OS threads. It also allows us to
handle the scheduling behaviour between different jobs such as waiting on another job.

## How to use it?
The job system can be used relatively simply.
Simply give it a lambda (or a function + data pointer) and expect it to run at some point in the future.
Assure that the data that you provide to the lambda will survive for the lifetime of the job
and is thread-safe.

Job chaining should be relatively simple. You can simply call "continueJob" alongside the source
job you should be waiting on. Once you wait on the job, it should just work.

TODO: explain job reservation flow and sleep/complete
*/

namespace IB
{
    constexpr uint32_t AllJobQueues = UINT32_MAX;
    enum class JobResult
    {
        Complete,
        Sleep
    };

    constexpr size_t MaxJobDataSize = 64;
    using JobFunc = JobResult(void *data);

    struct JobDesc
    {
        alignas(16) uint8_t JobData[MaxJobDataSize] = {};
        JobFunc *Func = nullptr;
        uint32_t QueueIndex = AllJobQueues;
    };

    struct JobHandle
    {
        uint64_t Value;
    };

    IB_API void initJobSystem();
    IB_API void killJobSystem();

    IB_API JobHandle launchJob(JobDesc desc);
    IB_API JobHandle continueJob(JobDesc desc, JobHandle* dependencies, uint32_t dependencyCount);

    // This continuation API allows us to put a job to sleep
    // but to execute it again in the future.
    // This allows us to wait on a job handle but have it execute multiple times
    // before signaling the waiting jobs.
    // When marking a job as complete, 2 things happen
    // - The waiting jobs are signaled
    // - The job generation is advanced
    // This makes our current job invalid.
    //
    // There are times however, where you might want to return from the job
    // and not signal the waiting jobs
    // then you might want to re-enter the job
    // and finally signal the waiting jobs
    // This can happen if I want to launch some jobs from my current job
    // then wait on those jobs
    // and finally return to the parent job to do some finalization work
    //
    // Putting a job to sleep will not signal the waiting jobs and will not advance the generation
    // meaning that you can wait on a sleeping job
    //
    // Typically in this context, you'll want to reference the job's handle from within the job
    // using the launchJob or continueJob API and storing the return type is erroneous.
    // Since the job might trigger and complete before the value is stored
    // As a result, you want to reserve the job to get the handle before launching the job.
    // An example of this is:
    // JobHandle = reserveJob(JobThatReferencesJobHandle)
    // launchJob(jobHandle)
    // Whereas
    // JobHandle = launchJob(JobThatReferencesJobHandle)
    // might introduce subtle bugs.
    //
    // Jobs can also be created with a QueueIndex that will
    // force them to run on a particular thread.
    // Consider avoiding this as much as possible due to the lack of parallelism
    // involved in putting a large chunk of jobs in the same queue.

    // Reserve a job for later execution
    IB_API JobHandle reserveJob(JobDesc desc);

    IB_API void launchJob(JobHandle handle);
    IB_API void continueJob(JobHandle handle, JobHandle* dependencies, uint32_t dependencyCount);

    // Utility API

    template <typename T>
    JobHandle launchJob(const T &functor, uint32_t queueIndex = AllJobQueues)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { return (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        desc.QueueIndex = queueIndex;
        return launchJob(desc);
    }

    template <typename T>
    JobHandle continueJob(const T &functor, JobHandle* dependencies, uint32_t dependencyCount, uint32_t queueIndex = AllJobQueues)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { return (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        desc.QueueIndex = queueIndex;
        return continueJob(desc, dependencies, dependencyCount);
    }

    template <typename T>
    JobHandle reserveJob(const T &functor, uint32_t queueIndex = AllJobQueues)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { return (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        desc.QueueIndex = queueIndex;
        return reserveJob(desc);
    }
} // namespace IB
