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
    enum class JobResult
    {
        Complete,
        Sleep
    };

    constexpr size_t MaxJobDataSize = 64;
    using JobFunc = JobResult(void *data);

    struct JobDesc
    {
        JobFunc *Func = nullptr;
        uint8_t JobData[MaxJobDataSize] = {};
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

    // Reserve a job for later execution
    IB_API JobHandle reserveJob(JobDesc desc);

    IB_API void launchJob(JobHandle handle);
    IB_API void continueJob(JobHandle handle, JobHandle* dependencies, uint32_t dependencyCount);

    // Utility API

    template <typename T>
    JobHandle launchJob(const T &functor)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { return (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        return launchJob(desc);
    }

    inline JobHandle launchJob(JobFunc *job, void *data)
    {
        return launchJob([job, data]() { return job(data); });
    }

    template <typename T>
    JobHandle continueJob(const T &functor, JobHandle* dependencies, uint32_t dependencyCount)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { return (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        return continueJob(desc, dependencies, dependencyCount);
    }

    inline JobHandle continueJob(JobFunc *job, void *data, JobHandle* dependencies, uint32_t dependencyCount)
    {
        return continueJob([job, data]() { return job(data); }, dependencies, dependencyCount);
    }

    template <typename T>
    JobHandle reserveJob(const T &functor)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { return (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        return reserveJob(desc);
    }

    inline JobHandle reserveJob(JobFunc *job, void *data)
    {
        return reserveJob([job, data]() { return job(data); });
    }
} // namespace IB
