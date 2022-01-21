#pragma once

#include "IBEngineAPI.h"
#include <stdint.h>
#include <string.h>
#include <type_traits>
#include <new>

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
    namespace Job
    {
        enum class Handle : uint64_t {};
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    }

    constexpr uint32_t NoJobType = UINT32_MAX;
    constexpr uint32_t AllJobQueues = UINT32_MAX;
    enum class JobResult
    {
        Complete,
        Sleep
    };

    constexpr size_t MaxJobDataSize = 128;
    using JobFunc = JobResult(void *data, Job::Handle currentJobHandle);

    struct JobDesc
    {
        alignas(16) uint8_t JobData[MaxJobDataSize] = {};
        JobFunc *Func = nullptr;
        uint32_t TypeIndex = NoJobType;
        uint32_t QueueIndex = AllJobQueues;
    };

    constexpr Job::Handle InvalidJob = Job::toHandle(UINT64_MAX);

    IB_API void initJobSystem();
    IB_API void killJobSystem();

    IB_API Job::Handle launchJob(JobDesc desc);
    IB_API Job::Handle continueJob(JobDesc desc, Job::Handle* dependencies, uint32_t dependencyCount);

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
    // JobHandle = reserveJob(JobThatReferencesJob::Handle)
    // launchJob(jobHandle)
    // Whereas
    // JobHandle = launchJob(JobThatReferencesJob::Handle)
    // might introduce subtle bugs.
    //
    // Jobs can also be created with a QueueIndex that will
    // force them to run on a particular thread.
    // Consider avoiding this as much as possible due to the lack of parallelism
    // involved in putting a large chunk of jobs in the same queue.

    // Reserve a job for later execution
    IB_API Job::Handle reserveJob(JobDesc desc);

    IB_API void launchJob(Job::Handle handle);
    IB_API void continueJob(Job::Handle handle, Job::Handle* dependencies, uint32_t dependencyCount);

    // TODO: If you end up waiting on a job that can only occur on a particular worker,
    // and the wait is also on that worker, you can end up deadlocking...
    IB_API void wait(Job::Handle* handles, uint32_t count);
    inline void wait(Job::Handle handle) { wait(&handle, 1); }

    // Utility API

    template< typename T>
    JobResult jobFunctor(void *functorData, Job::Handle currentJob)
    {
        T* functor = reinterpret_cast<T *>(functorData);
        JobResult result = (*functor)(currentJob);
        if (result == JobResult::Complete)
        {
            functor->~T(); // Clean up our data if we're done with it.
        }
        return result;
    }

    template <typename T>
    Job::Handle launchJob(T &&functor, uint32_t typeIndex = NoJobType, uint32_t queueIndex = AllJobQueues)
    {
        using RawType = typename std::remove_reference<T>::type;

        static_assert(sizeof(RawType) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = &jobFunctor<RawType>;
        new (desc.JobData) RawType{ std::forward<T>(functor) };
        desc.TypeIndex = typeIndex;
        desc.QueueIndex = queueIndex;
        return launchJob(desc);
    }

    template <typename T>
    Job::Handle continueJob(T &&functor, Job::Handle *dependencies, uint32_t dependencyCount, uint32_t typeIndex = NoJobType, uint32_t queueIndex = AllJobQueues)
    {
        using RawType = typename std::remove_reference<T>::type;

        static_assert(sizeof(RawType) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = &jobFunctor<RawType>;
        new (desc.JobData) RawType{ std::forward<T>(functor) };
        desc.TypeIndex = typeIndex;
        desc.QueueIndex = queueIndex;
        return continueJob(desc, dependencies, dependencyCount);
    }

    template <typename T>
    Job::Handle reserveJob(T &&functor, uint32_t typeIndex = NoJobType, uint32_t queueIndex = AllJobQueues)
    {
        using RawType = typename std::remove_reference<T>::type;

        static_assert(sizeof(RawType) <= MaxJobDataSize, "Functor is too large for job. Consider allocating it on the heap.");

        JobDesc desc;
        desc.Func = &jobFunctor<RawType>;
        new (desc.JobData) RawType{ std::forward<T>(functor) };
        desc.TypeIndex = typeIndex;
        desc.QueueIndex = queueIndex;
        return reserveJob(desc);
    }
} // namespace IB
