#include "IBJobs.h"
#include "IBPlatform.h"
#include "IBLogging.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

/*
## Multi-threading
Multi-threading is a fairly involved and complicated topic.
As a result, we'll quickly gloss over out-of-order processors and compiler reordering.

### Out-of-order processors
"Modern" CPU processors are typically out-of-order processors.
You can think of an in-order processor as a simple machine that consumes
instructions in the order that they arrived. Instead, an out-of-order processor
can start executing instructions that are further down the pipe
if the instructions they depend on has already completed execution.

As an example, imagine that we're feeding our processor with some instructions: A, B, C, D
If instruction B depends on instruction A, then our processor has to execute A then B
However, if instruction C does not depend on instruction A or B and does not need the same
execution unit as the other 2 (maybe it simply uses a memory unit instead of the ALU)
Then it can execute while A is executing or while B is executing.

This is important because this means that our store could theoretically
complete before another store instruction. (Say instruction B was a store instruction).

As a result, if C completed before B, then a thread could see the changes to C but the changes to B
might not be visible yet. (This is not the case for intel's CPUs as of this writting, however moving to a weaker memory model would make this the case)

You can guarantee that B has completed it's store to memory by inserting a store fence in between B and C.
Something like this:
B
Store Fence
C

This would assure that all stores before the store fence are completed before C is executed.
This is useful if we have a flag that tells our other threads that data is ready.

### Compiler Reordering
It is also possible for our compiler to change the ordering of our instructions if it determines that it could improve performance.
As a result, the compiler might say "I would like to move C before B to improve performance".

In some scenarios, you want to make sure that the order in which you read/write things is maintained.
As a result you can add a compiler barrier to assure that specific instructions aren't reordered accross a boundary.

Typically, using a store fence or load fence will achieve the same results as a pure compiler barrier.

### Volatile Store/Load
You'll noticed that we use volatileStore and volatileLoad quite a bit here.
This is because our compiler can decide to optimize a load away, load it once or not load it at all
if it thinks it will not impact the single-threaded behaviour of the program.

volatileLoad and volatileStore is a way of telling the compiler "Don't optimize this load, it might have changed because of someone else"

### Job Systems
Now that we've spoken a little bit about some of the intricacies of multi-threading, we can talk about our job system.
The purpose of a job system is to minimize the creation of threads and to instead break up work
into a series of small chunks of work. "Jobs"

The reason we don't want to be creating and destroying threads continuously is because
this has a substantial performance impact. Instead, we want to create our threads beforehand
and feed them work as we have it.

We feed work through a set of worker thread specific queues that store a list of all the jobs that a
worker thread is expected to complete. We want a queue per thread in order to reduce contention.
If we were to include a global list, all threads would have to compete with each other to pull something
off of our global queue. This would require either a lock-free approach to the global queue or a global lock.
Both are acceptable, however, if we have a single queue per worker
then our threads can pull from the queue without any atomic operations or locks.
This also means that they don't have to "try again" if a thread has preempted them to the global queue.

A potential downside, is that some threads might have a lighter workload than others and having to wait for longer periods of time.
If that ends up being the case, we can look into implementing a job stealing algorithm where a thread
can pull work from another thread if it runs out of work for a while.

### Job Queues
Our job queue is implemented as a fixed sized ring buffer.
This means that the queue cannot be resized. Which allows us to avoid having to deal with
the challenges of resizing the queue.

Our queue has 2 indices, a consumer index and a producer index.
The consumer index is in charge of retrieving work from the queue
and the producer index is in charge of assing work to the queue.

Our approach is implemented in a lock free manner using this algorithm:
The consumer (worker thread) is the only one that can move the consumer index forward. This means that
it can do so without doing any atomic operations,

The consumer will move the consumer index forward once it has completed the work assigned to that index.
As a result, if there is no work at the index, the consumer will simply wait for work to appear there.

This means that the producer (threads launching jobs) must feed the consumer sequentially.

This approach of our consumer waiting for work to appear allows our consumer to be fairly lenient to work
taking some time to appear in it's queue.

The producer side of our queue is where we introduce some atomic operations to assure that no threads
introduce a race condition.

The idea for our approach is fairly simple. Our thread will simply iterate through each worker
and determine if that worker has some space in it's queue.

If it has space, it will try to commit a slot by moving the producer index forward.
If it succeeds at moving its producer index forward, that means the previous index is now commited
and it can write data to it safely.
We can guarantee this behaviour using a compare and exchange.

Once we know we can safely write to an index, we want the transfer of data to be as simple as possible.
If we have to send a lot of data, our consumer would have to be notified when all of the data has been written.
Thankfully, we only write a single pointer to the shared slot in the queue.
We can expect this operation to be atomic and the consumer thread should not see any partial writes. (Needs source)

Once our pointer has been written, we want to wake up our waiting thread (it might have gone to sleep while waiting to save cycles and allow the OS to switch to a more useful thread.)

### References
https://preshing.com/20120625/memory-ordering-at-compile-time/
https://stackoverflow.com/questions/4537753/when-should-i-use-mm-sfence-mm-lfence-and-mm-mfence

*/

namespace IB
{
    namespace Job
    {
        namespace
        {
#pragma warning(disable : 4324) // We don't care about the padding complaint here.
            struct alignas(64) Data
            {
                alignas(16) uint8_t Data[IB::MaxJobDataSize];
                IB::JobFunc* Func = nullptr;
                uint32_t Generation = 0;
                uint32_t QueueIndex = IB::AllJobQueues;
            };
#pragma warning(default : 4324)
        }
    }
}

namespace
{
    uint32_t const WorkerCount = IB::processorCount();

    constexpr uint32_t MaxJobCount = 1024;

    struct JobQueue
    {
        IB::Job::Data *Jobs[MaxJobCount] = {};  // Mark as volatile, we don't want to cache our loads.
        uint32_t Producer = 0; // Represents where we're going to write next
        uint32_t Consumer = 0;        // Represents where we're going to read next, advance before reading
    };

    struct WorkerThread
    {
        JobQueue Queue;
        IB::ThreadHandle Thread;
        IB::ThreadEvent SleepEvent;
        uint32_t Index;
        bool Alive = false;
    };
    constexpr uint32_t MaxWorkerCount = 64;
    WorkerThread Workers[MaxWorkerCount];

    constexpr uint32_t MaxJobPoolCount = MaxJobCount * MaxWorkerCount;
    IB::Job::Data JobPool[MaxJobPoolCount] = {};
    IB::Job::Data EmptyJob = { {}, +[](void*) { return IB::JobResult::Complete; } };

    constexpr uint32_t MaxWaitCount = 1 << 16;
    constexpr uint32_t MaxJobWaiters = 10;
    struct
    {
        uint64_t Waiters[MaxJobPoolCount][MaxJobWaiters];
        uint32_t WaitCounts[MaxWaitCount];
    } WaitList;

    IB::Job::Data* takeJob(IB::JobDesc desc)
    {
        IB::Job::Data *job = nullptr;

        // Get a job from our pool
        // Many threads can be trying to pull from the pool at the same time
        // Assure that we can commit one of the elements to ourselves using a compare exchange
        uint32_t jobIndex = 0;
        for (; jobIndex < MaxJobPoolCount; jobIndex++)
        {
            // Everyone is trying to get a job from the pool, make sure that we've taken it with a compare exchange.
            if (IB::atomicCompareExchange<IB::JobFunc>(&JobPool[jobIndex].Func, nullptr, desc.Func) == nullptr)
            {
                IB::threadAcquire(); // Assure that generation loads aren't run speculatively
                job = &JobPool[jobIndex];
                break;
            }
        }

        // Our job is free to be written to at this point.
        IB_ASSERT(job != nullptr, "Failed to get a job from the job pool!");
        static_assert(sizeof(job->Data) == sizeof(desc.JobData), "Job description data size doesn't match job data size.");
        memcpy(job->Data, desc.JobData, sizeof(desc.JobData));
        job->QueueIndex = desc.QueueIndex;

        return job;
    }

    IB::Job::Data* stealJob(uint32_t workerIndex)
    {
        // Steal jump is an attempt at making stealing fair by trying to steal from the same level of each queue before moving on to the next job.
        JobQueue *queue = &Workers[workerIndex].Queue;

        int32_t currentProducerIndex = static_cast<int32_t>(IB::volatileLoad(&queue->Producer));

        // Loop for our queue size estimate. It's okay if we go behind our consumer here
        // we'll simply read null and move on
        int32_t jobCount = abs(currentProducerIndex - static_cast<int32_t>(IB::volatileLoad(&queue->Consumer)));
        for (int32_t i = 1; i <= jobCount; i++)
        {
            int32_t stealProducerIndex = currentProducerIndex - i;
            if (stealProducerIndex < 0)
            {
                stealProducerIndex = MaxJobCount + stealProducerIndex;
            }

            IB::Job::Data* jobCandidate = queue->Jobs[stealProducerIndex];
            if (jobCandidate != nullptr && jobCandidate != &EmptyJob && jobCandidate->QueueIndex == IB::AllJobQueues)
            {
                // Simply replace the last job with our current job.
                // If it was a valid job, our worker will simply work on the empty job.
                //   If it was a valid job, our worker will not have consumed it. If our worker consumed it,
                //   The job would have been exchanged with a nullptr and that's what we would receive here.
                // If it was a nullptr, we will simply continue trying to steal
                return IB::atomicExchange(&queue->Jobs[stealProducerIndex], &EmptyJob);
            }
        }

        return nullptr;
    }

    thread_local uint32_t NextWorker = 0;
    void commitJob(IB::Job::Data* job)
    {
        // Keep trying to allocator to various workers.
        // If all the worker queues are full, then we'll iterate until they aren't.
        // If this ends up being a problem, we can add a thread event to make our thread sleep.
        uint32_t commitedWorkerIndex = UINT32_MAX;
        uint32_t commitedJobIndex = UINT32_MAX;
        while (commitedWorkerIndex == UINT32_MAX) // Until we've commited to a worker.
        {
            // Simply increment our value, use the modulo as our indexing.
            uint32_t worker = (job->QueueIndex == IB::AllJobQueues ? NextWorker++ : job->QueueIndex) % WorkerCount;

            // If our queue has space, try to commit our slot by doing a compare and exchange.
            // If it succeeds then we've commited our producer index and moved the index forward.
            // This will be visible to the worker thread,
            // however it will simply wait to make sure that it has a job to do before
            // it moves on to the next element.
            uint32_t currentProducerIndex = IB::volatileLoad(&Workers[worker].Queue.Producer);
            uint32_t nextProducerIndex = (currentProducerIndex + 1) % MaxJobCount;
            while (nextProducerIndex != IB::volatileLoad(&Workers[worker].Queue.Consumer)) // Do we still have space in this queue? Keep trying.
            {
                if (IB::atomicCompareExchange(&Workers[worker].Queue.Producer, currentProducerIndex, nextProducerIndex) == currentProducerIndex)
                {
                    // If we succesfuly commited to our producer index, we can use these indices.
                    commitedWorkerIndex = worker;
                    commitedJobIndex = currentProducerIndex;
                    break;
                }
                else
                {
                    currentProducerIndex = IB::volatileLoad(&Workers[worker].Queue.Producer);
                    nextProducerIndex = (currentProducerIndex + 1) % MaxJobCount;
                }
            }
        }

        // If our thread is preempted before we set our job,
        // then our worker thread will simply sleep until the job has actually been written to the variable.
        // We will then wake it up after we're done writting it out.
        IB::Job::Data* previousJobValue = IB::atomicExchange(&Workers[commitedWorkerIndex].Queue.Jobs[commitedJobIndex], job);
        // An empty job here is fine, it's possible a worker tried to steal from a consumed index
        IB_ASSERT(previousJobValue == nullptr || previousJobValue == &EmptyJob, "We're expecting our job to be null here! Did someone write to it before us?!?");

        IB::threadRelease(); // Assure our writes are globally visible before this event
        IB::signalThreadEvent(Workers[commitedWorkerIndex].SleepEvent); // Signal our sleeping worker
    }

    void waitJob(IB::Job::Data* job, IB::Job::Handle* dependencies, uint32_t dependencyCount)
    {
        uint32_t validDependencyCount = dependencyCount;
        for (uint32_t dep = 0; dep < dependencyCount; dep++)
        {
            if (IB::Job::toValue(dependencies[dep]) == IB::InvalidJob)
            {
                validDependencyCount--;
            }
        }

        if (validDependencyCount == 0)
        {
            commitJob(job);
            return;
        }

        // Try to commit a wait counter for our dependency count.
        uint32_t waitIndex = 0;
        for (; waitIndex < MaxWaitCount; waitIndex++)
        {
            if (IB::atomicCompareExchange(&WaitList.WaitCounts[waitIndex], 0, validDependencyCount + 1) == 0)
            {
                break;
            }
        }
        IB_ASSERT(waitIndex != MaxWaitCount, "Failed to commit a wait counter!");

        // Add our job to the wait list of all our dependencies
        for (uint32_t dep = 0; dep < dependencyCount; dep++)
        {
            if (IB::Job::toValue(dependencies[dep]) == IB::InvalidJob)
            {
                continue;
            }

            uint32_t sourceJobIndex = IB::Job::toValue(dependencies[dep]) & 0xFFFFFFFF;
            uint32_t sourceJobGeneration = IB::Job::toValue(dependencies[dep]) >> 32;

            uint16_t jobIndex = static_cast<uint16_t>(job - JobPool);
            uint64_t waitHandle = ((static_cast<uint64_t>(sourceJobGeneration) + 1) << 32) | (waitIndex << 16) | jobIndex;

            uint32_t listIndex = UINT32_MAX;
            while (listIndex == UINT32_MAX)
            {
                for (uint32_t i = 0; i < MaxJobWaiters; i++)
                {
                    if (IB::atomicCompareExchange(&WaitList.Waiters[sourceJobIndex][i], 0, waitHandle) == 0)
                    {
                        listIndex = i;
                        break;
                    }
                }
            }

            // At this point, we could get preempted and the dependency jobs could take our job from the wait list

            // If our job has moved on from our generation, that means it's complete.
            // We need to do our generation check after adding to the list because if we add to the list after our job
            // this situation could occur:
            // - Our generation check fails
            // - Dependency job preempts us and completes
            // - We add our job to the list
            // Now we won't get our job completed.

            // With the add to the list first, if that occurs we get:
            // - We add to the list
            // - Job completes and removes from the list
            // or
            // - We add to the list
            // - Generation check fails
            // - Job completes and removes from the list
            if (IB::volatileLoad(&JobPool[sourceJobIndex].Generation) > sourceJobGeneration)
            {
                // At this point, there's 3 possibilities.
                // 1. No one is contending with us, we simply clear our wait index and commit our job
                // 2. The job preempted us and commited the job for us
                // 3. The job preempted us, commited the job for us and then someone else added their job to the list

                // waitHandle cannot be equal to our handle for another job at this point because we've taken that job
                // index from the job pool.
                if (IB::atomicCompareExchange(&WaitList.Waiters[sourceJobIndex][listIndex], waitHandle, 0) == waitHandle)
                {
                    IB_ASSERT(WaitList.WaitCounts[waitIndex] > 1, "Value should be above 1. 1 is our final value.");
                    if (IB::atomicDecrement(&WaitList.WaitCounts[waitIndex]) == 1)
                    {
                        commitJob(job);
                        // only once we've commited our job do we return the wait count to the pool.
                        IB::volatileStore<uint32_t>(&WaitList.WaitCounts[waitIndex], 0);
                    }
                }
            }
        }
    }

    void workerFunc(void *data)
    {
        WorkerThread *worker = reinterpret_cast<WorkerThread *>(data);
        JobQueue *queue = &worker->Queue;

        while (true)
        {
            IB::Job::Data* stolenJob = nullptr;
            // Keep looping while:
            // We're alive and waiting for a job or our producer hasn't moved forward
            while (
                (IB::volatileLoad(&queue->Jobs[queue->Consumer]) == nullptr
                || IB::volatileLoad(&queue->Producer) == queue->Consumer)
                && IB::volatileLoad(&worker->Alive))
            {
                // Give it a few iterations, we might just be writting the value to this index.
                bool breakEarly = false;
                for (uint32_t i = 0; i < 32; i++)
                {
                    if ((IB::volatileLoad(&queue->Jobs[queue->Consumer]) != nullptr
                        && IB::volatileLoad(&queue->Producer) != queue->Consumer)
                        || !IB::volatileLoad(&worker->Alive))
                    {
                        breakEarly = true;
                        break;
                    }
                }

                if (breakEarly)
                {
                    break;
                }

                // Try to take from each of our workers
                for (uint32_t i = 1; i < WorkerCount; i++)
                {
                    uint32_t stealingWorkerIndex = (worker->Index + i) % WorkerCount;
                    stolenJob = stealJob(stealingWorkerIndex);
                    if (stolenJob != nullptr)
                    {
                        break;
                    }
                }

                if (stolenJob != nullptr)
                {
                    break;
                }
                else
                {
                    // Should go to sleep here after a few iterations.
                    // Possible behaviour for this wait:
                    // - No work is available, next signal will wake us when we add work
                    // - Work is available but not visible to us yet, signal will wake us when it's visible
                    // - Signal has been sent for a previous chunk of work
                    //     Say we signaled for Consumer - 1,
                    //     If we signaled for that index and it wasn't consumed
                    //     then we'll simply reset the event
                    //     and return to the iteration to wait for the next signal
                    // There are no states (that I can think of) that will leave us waiting for a signal
                    // even if there is work available.
                    constexpr uint32_t wait = 1; // Wait for 1 millisecond
                    waitOnThreadEvent(worker->SleepEvent, wait);
                    IB::threadAcquire(); // Assure our loads aren't run before this event
                }
            }

            if (!IB::volatileLoad(&worker->Alive))
            {
                break;
            }

            IB::Job::Data *job;
            if (stolenJob == nullptr)
            {
                job = IB::atomicExchange<IB::Job::Data>(&queue->Jobs[queue->Consumer], nullptr);
            }
            else
            {
                job = stolenJob;
            }

            // Note that our job might have marked itself for continuation while it's also active in another thread.
            // If it was put to sleep in this result, it will simply be removed from the queue
            // As a result, it should behave correctly if the job completes before it is even put to sleep.
            // If putting to sleep has side effects in the future, the API might have to be re-thought
            IB::JobResult result = job->Func(job->Data);

            // Queue management
            if(stolenJob == nullptr)
            {
                IB::volatileStore(&queue->Consumer, (queue->Consumer + 1) % MaxJobCount);
            }

            // A sleeping job will not be returned to the job pool and waiting
            // jobs will not be signaled.
            // Don't do any threadpool management and sleeping job signaling for an empty job.
            if (result == IB::JobResult::Complete && job != &EmptyJob)
            {
                // Thread pool management
                uint32_t generation = IB::volatileLoad(&job->Generation);
                IB::volatileStore(&job->Generation, generation + 1);

                // Attempt to signal all our waiting jobs
                uint32_t jobIndex = static_cast<uint32_t>(job - JobPool);
                for (uint32_t i = 0; i < MaxJobWaiters; i++)
                {
                    uint64_t waitHandle = IB::volatileLoad(&WaitList.Waiters[jobIndex][i]);
                    // If we fail this branch and move on,
                    // and then the continueJob writes to that slot for this job,
                    // then that means our generation index was already incremented.
                    // and as a result the continueJob function will immediatly commit the waiting job.
                    if (waitHandle != 0)
                    {
                        uint32_t targetGeneration = (waitHandle >> 32) - 1; // We added 1 to our generation to make sure that our handle is non zero
                        uint32_t theirIndex = (waitHandle & 0xFFFF);
                        uint32_t waitIndex = (waitHandle >> 16) & 0xFFFF;

                        if (targetGeneration == generation)
                        {
                            // Attempt to take the job from the list of waiters.
                            // If we failed to take it, ignore it,
                            // it means the job was committed by the "continueJob" call.
                            if (IB::atomicCompareExchange(&WaitList.Waiters[jobIndex][i], waitHandle, 0) == waitHandle)
                            {
                                IB_ASSERT(WaitList.WaitCounts[waitIndex] > 1, "Value should be above 1. 1 is our final value.");
                                if (IB::atomicDecrement(&WaitList.WaitCounts[waitIndex]) == 1)
                                {
                                    commitJob(&JobPool[theirIndex]);
                                    IB::volatileStore<uint32_t>(&WaitList.WaitCounts[waitIndex], 0);
                                }
                            }
                        }
                    }
                }

                // Only return our job to the pool once we've signaled our waiters
                {
                    // Assure that our generation increment is visible before we return our job to the pool
                    // If it wasn't, our job could be pulled from the pool,
                    // and a handle created with the wrong generation index.
                    IB::threadRelease();
                    IB::volatileStore<IB::JobFunc *volatile>(&job->Func, nullptr); // Returns our job to the pool
                }
            }
        }
    }
} // namespace

namespace IB
{
    void initJobSystem()
    {
        memset(&WaitList, 0, sizeof(WaitList)); // Zero at runtime. Compiler hates zeroing this out.
        for (uint32_t i = 0; i < WorkerCount; i++)
        {
            Workers[i].Alive = true;
            Workers[i].SleepEvent = createThreadEvent();
            Workers[i].Thread = createThread(&workerFunc, &Workers[i]);
            Workers[i].Index = i;
        }
    }

    void killJobSystem()
    {
        ThreadHandle threads[MaxWorkerCount];
        for (uint32_t i = 0; i < WorkerCount; i++)
        {
            volatileStore(&Workers[i].Alive, false);
            threads[i] = Workers[i].Thread;

            IB::threadRelease();
            IB::signalThreadEvent(Workers[i].SleepEvent);
        }

        waitOnThreads(threads, WorkerCount);
        for (uint32_t i = 0; i < WorkerCount; i++)
        {
            destroyThread(Workers[i].Thread);
            destroyThreadEvent(Workers[i].SleepEvent);
        }
    }

    Job::Handle reserveJob(JobDesc desc)
    {
        Job::Data* job = takeJob(desc);
        return Job::toHandle((static_cast<uint64_t>(job->Generation) << 32) | static_cast<uint32_t>(job - JobPool));
    }

    Job::Handle continueJob(JobDesc desc, Job::Handle* dependencies, uint32_t dependencyCount)
    {
        Job::Data* job = takeJob(desc);
        // Retrieve our job generation before we potentially commit our job.
        // if might execute and complete before we even reach our return.
        uint32_t jobGeneration = volatileLoad(&job->Generation);
        waitJob(job, dependencies, dependencyCount);
        return Job::toHandle((static_cast<uint64_t>(jobGeneration) << 32) | static_cast<uint32_t>(job - JobPool));
    }

    void continueJob(Job::Handle handle, Job::Handle* dependencies, uint32_t dependencyCount)
    {
        uint32_t jobIndex = IB::Job::toValue(handle) & 0xFFFFFFFF;
        uint32_t generation = IB::Job::toValue(handle) >> 32;
        Job::Data* job = &JobPool[jobIndex];
        IB_ASSERT(job->Generation == generation, "Asking to continue a job that was completed. Did we put it to sleep?");
        waitJob(job, dependencies, dependencyCount);
    }

    void launchJob(Job::Handle handle)
    {
        uint32_t jobIndex = IB::Job::toValue(handle) & 0xFFFFFFFF;
        uint32_t generation = IB::Job::toValue(handle) >> 32;
        Job::Data* job = &JobPool[jobIndex];
        IB_ASSERT(job->Generation == generation, "Asking to continue a job that was completed. Did we put it to sleep?");
        commitJob(job);
    }

    Job::Handle launchJob(JobDesc desc)
    {
        Job::Data* job = takeJob(desc);
        // Grab our job generation before we commit it.
        // The job generation might increment if we get preempted before our return.
        uint32_t jobGeneration = volatileLoad(&job->Generation);
        commitJob(job);
        return Job::toHandle((static_cast<uint64_t>(jobGeneration) << 32) | static_cast<uint32_t>(job - JobPool));
    }
} // namespace IB
