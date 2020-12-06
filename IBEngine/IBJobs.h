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

TODO: Add some information about chaining lambdas when that API is added.
*/

namespace IB
{
    constexpr size_t MaxJobDataSize = 64;
    using JobFunc = void(void *data);

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
    IB_API JobHandle continueJob(JobDesc desc, JobHandle sourceJob);

    template <typename T>
    JobHandle launchJob(const T &functor)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        return launchJob(desc);
    }

    inline JobHandle launchJob(JobFunc *job, void *data)
    {
        return launchJob([job, data]() { job(data); });
    }

    template <typename T>
    JobHandle continueJob(const T &functor, JobHandle sourceJob)
    {
        static_assert(sizeof(T) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = [](void *functor) { (*reinterpret_cast<T *>(functor))(); };
        memcpy(desc.JobData, &functor, sizeof(T));
        return continueJob(desc, sourceJob);
    }

    inline JobHandle continueJob(JobFunc *job, void *data, JobHandle sourceJob)
    {
        return continueJob([job, data]() { job(data); }, sourceJob);
    }
} // namespace IB
