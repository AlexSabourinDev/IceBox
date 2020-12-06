#include "IBJobs.h"
#include "Platform/IBPlatform.h"
#include "IBLogging.h"

#include <string.h>

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

namespace
{
    template <typename T>
    T volatileLoad(T const *target)
    {
        return *static_cast<T volatile const *>(target);
    }

    template <typename T>
    void volatileStore(T *target, T value)
    {
        *static_cast<T volatile *>(target) = value;
    }

    uint32_t const WorkerCount = IB::processorCount();

    constexpr uint32_t MaxJobCount = 1024;
#pragma warning(disable : 4324) // We don't care about the padding complaint here.
    struct alignas(64) Job
    {
        uint8_t Data[IB::MaxJobDataSize];
        IB::AtomicPtr Func = {nullptr};
    };
#pragma warning(default : 4324)

    struct JobQueue
    {
        Job *Jobs[MaxJobCount] = {};  // Mark as volatile, we don't want to cache our loads.
        IB::AtomicU32 Producer = {0}; // Represents where we're going to write next
        uint32_t Consumer = 0;        // Represents where we're going to read next, advance before reading
    };

    struct WorkerThread
    {
        JobQueue Queue;
        IB::ThreadHandle Thread;
        IB::ThreadEvent SleepEvent;
        bool Alive = false;
    };
    constexpr uint32_t MaxWorkerCount = 1024;
    WorkerThread Workers[MaxWorkerCount];

    constexpr uint32_t MaxJobPoolCount = MaxJobCount * MaxWorkerCount;
    Job JobPool[MaxJobPoolCount] = {};

    void WorkerFunc(void *data)
    {
        WorkerThread *worker = reinterpret_cast<WorkerThread *>(data);
        JobQueue *queue = &worker->Queue;

        while (true)
        {
            while (volatileLoad(&queue->Jobs[queue->Consumer]) == nullptr && volatileLoad(&worker->Alive))
            {
                // Give it a few iterations, we might just be writting the value to this index.
                bool breakEarly = false;
                for (uint32_t i = 0; i < 32; i++)
                {
                    if (volatileLoad(&queue->Jobs[queue->Consumer]) != nullptr || !volatileLoad(&worker->Alive))
                    {
                        breakEarly = true;
                        break;
                    }
                }

                if (breakEarly)
                {
                    break;
                }

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
                waitOnThreadEvent(worker->SleepEvent);
            }

            if (!volatileLoad(&worker->Alive))
            {
                break;
            }

            Job *job = volatileLoad(&queue->Jobs[queue->Consumer]);
            reinterpret_cast<IB::JobFunc *>(job->Func.Value)(job->Data);
            volatileStore<void *volatile>(&job->Func.Value, nullptr);

            // Make sure other threads see our writes to the job queue before we move our consumer index forward.
            // This fence is not necessary, but having it allows us to have some extra
            // asserts on the producer side to make sure producers aren't stomping on each other.
            IB::threadStoreFence();
            volatileStore(&queue->Consumer, (queue->Consumer + 1) % MaxJobCount);
        }
    }

    thread_local uint32_t NextWorker = 0;
} // namespace

namespace IB
{
    void initJobSystem()
    {
        for (uint32_t i = 0; i < WorkerCount; i++)
        {
            Workers[i].Alive = true;
            Workers[i].Thread = createThread(&WorkerFunc, &Workers[i]);
            Workers[i].SleepEvent = createThreadEvent();
        }
    }

    void killJobSystem()
    {
        ThreadHandle threads[MaxWorkerCount];
        for (uint32_t i = 0; i < WorkerCount; i++)
        {
            volatileStore(&Workers[i].Alive, false);
            threads[i] = Workers[i].Thread;

            IB::threadStoreFence();
            IB::signalThreadEvent(Workers[i].SleepEvent);
        }

        waitOnThreads(threads, WorkerCount);
        for (uint32_t i = 0; i < WorkerCount; i++)
        {
            destroyThread(Workers[i].Thread);
            destroyThreadEvent(Workers[i].SleepEvent);
        }
    }

    void launchJob(JobDesc desc)
    {
        Job *job = nullptr;

        // Get a job from our pool
        // Many threads can be trying to pull from the pool at the same time
        // Assure that we can commit one of the elements to ourselves using a compare exchange
        for (uint32_t i = 0; i < MaxJobPoolCount; i++)
        {
            // Everyone is trying to get a job from the pool, make sure that we've taken it with a compare exchange.
            if (atomicCompareExchange(&JobPool[i].Func, nullptr, desc.Func) == nullptr)
            {
                job = &JobPool[i];
                break;
            }
        }

        IB_ASSERT(job != nullptr, "Failed to get a job from the job pool!");
        static_assert(sizeof(job->Data) == sizeof(desc.JobData), "Job description data size doesn't match job data size.");
        memcpy(job->Data, desc.JobData, sizeof(desc.JobData));

        // Keep trying to allocator to various workers.
        // If all the worker queues are full, then we'll iterate until they aren't.
        // If this ends up being a problem, we can add a thread event to make our thread sleep.
        uint32_t commitedWorkerIndex = UINT32_MAX;
        uint32_t commitedJobIndex = UINT32_MAX;
        while (commitedWorkerIndex == UINT32_MAX) // Until we've commited to a worker.
        {
            // Simply increment our value, use the modulo as our indexing.
            uint32_t worker = (NextWorker++) % WorkerCount;

            // If our queue has space, try to commit our slot by doing a compare and exchange.
            // If it succeeds then we've commited our producer index and moved the index forward.
            // This will be visible to the worker thread,
            // however it will simply wait to make sure that it has a job to do before
            // it moves on to the next element.
            uint32_t currentProducerIndex = Workers[worker].Queue.Producer.Value;
            uint32_t nextProducerIndex = (currentProducerIndex + 1) % MaxJobCount;
            while (nextProducerIndex != volatileLoad(&Workers[worker].Queue.Consumer)) // Do we still have space in this queue? Keep trying.
            {
                if (atomicCompareExchange(&Workers[worker].Queue.Producer, currentProducerIndex, nextProducerIndex) == currentProducerIndex)
                {
                    // If we succesfuly commited to our producer index, we can use these indices.
                    commitedWorkerIndex = worker;
                    commitedJobIndex = currentProducerIndex;
                    break;
                }
                else
                {
                    currentProducerIndex = Workers[worker].Queue.Producer.Value;
                    nextProducerIndex = (currentProducerIndex + 1) % MaxJobCount;
                }
            }
        }

        // If our thread is preempted before we set out job,
        // then our worker thread will simply sleep until the job has actually been written to the variable.
        // We will then wake it up after we're done writting it out.
        IB_ASSERT(Workers[commitedWorkerIndex].Queue.Jobs[commitedJobIndex] == nullptr, "We're expecting our job to be null here! Did someone write to it before us?!?");
        volatileStore(&Workers[commitedWorkerIndex].Queue.Jobs[commitedJobIndex], job);

        // Assure that our writes are globally visible before we wake up our worker thread.
        // We can avoid having it iterate uselessly one more time.
        // This fence is not neccessary but a convenience for our worker thread.
        threadStoreFence();
        signalThreadEvent(Workers[commitedWorkerIndex].SleepEvent); // Signal our sleeping worker
    }
} // namespace IB
