
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBPlatform.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBLogging.h>
#include <stdio.h>
#include <Windows.h>

uint32_t volatile Counter = 0;
int main()
{
    IB::initJobSystem();

    // Test simple jobs incrementing a global counter.
    uint32_t constexpr iterations = 1024 * 10;
    for (uint32_t i = 0; i < iterations; i++)
    {
        IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);
            printf("%u ", Counter);
            return IB::JobResult::Complete;
        });
    }

    while (Counter != iterations) {}

    // Spawn job from job
    Counter = 0;
    for (uint32_t i = 0; i < iterations / 10; i++)
    {
        IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            for (uint32_t i = 0; i < 10; i++)
            {
                IB::launchJob([i](IB::Job::Handle /*currentJob*/)
                {
                    IB::atomicIncrement(&Counter);
                    printf("%u: %u ", i, Counter);
                    return IB::JobResult::Complete;
                });
            }
            return IB::JobResult::Complete;
        });
    }

    while (Counter != iterations) {}

    for (uint32_t i = 0; i < 10; i++)
    {
        IB::Job::Handle sleepJob = IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            Sleep(100);
            return IB::JobResult::Complete;
        });
        IB::Job::Handle continueJob1 = IB::continueJob([](IB::Job::Handle /*currentJob*/)
        {
            Sleep(10);
            return IB::JobResult::Complete;
        }, &sleepJob, 1);
        IB::Job::Handle continueJob2 = IB::continueJob([](IB::Job::Handle /*currentJob*/)
        {
            Sleep(10);
            return IB::JobResult::Complete;
        }, &sleepJob, 1);

        Counter = 0;
        IB::continueJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }, &continueJob1, 1);

        IB::continueJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }, &continueJob2, 1);

        while (Counter != 2) {}
    }

    for (uint32_t i = 0; i < 10; i++)
    {
        IB::Job::Handle sleepJob = IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            Sleep(100);
            return IB::JobResult::Complete;
        });

        IB::Job::Handle continueJobs[10];
        for (uint32_t j = 0; j < 10; j++)
        {
            continueJobs[j] = IB::continueJob([](IB::Job::Handle /*currentJob*/)
            {
                Sleep(10);
                return IB::JobResult::Complete;
            }, &sleepJob, 1);
        }

        Counter = 0;
        IB::continueJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }, continueJobs, 10);

        while (Counter != 1) {}
    }

    IB::Job::Handle myHandle;
    myHandle = IB::reserveJob([&myHandle](IB::Job::Handle /*currentJob*/)
    {
        if (Counter == 0)
        {
            IB::Job::Handle childJob = IB::launchJob([](IB::Job::Handle /*currentJob*/)
            {
                Sleep(10);
                return IB::JobResult::Complete;
            });

            IB::continueJob(myHandle, &childJob, 1);
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Sleep;
        }
        else
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }
    });

    Counter = 0;
    IB::launchJob(myHandle);

    while (Counter != 2) {}

    // Small memory allocations
    Counter = 0;
    float volatile* initialFloat = IB::allocate<float>(2.0f);
    for (uint32_t i = 0; i < iterations; i++)
    {
        IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);

            // Try to hammer the allocator as much as possible.
            float* value = IB::allocate<float>(1.0f);
            printf("%f ", *value);
            IB::deallocate(value);

            return IB::JobResult::Complete;
        });
    }
    while (Counter != iterations) {}
    IB_ASSERT(*initialFloat == 2.0f, "Our float changed value!");
    IB::deallocate((float*)initialFloat);

    // Medium memory allocations
    Counter = 0;
    for (uint32_t i = 0; i < iterations; i++)
    {
        IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);

            // Try to hammer the allocator as much as possible.
            void* value = IB::memoryAllocate(4096, 4);
            printf("allocation ");
            IB::memoryFree(value);

            return IB::JobResult::Complete;
        });
    }
    while (Counter != iterations) {}

    // large memory allocations
    Counter = 0;
    for (uint32_t i = 0; i < 20; i++)
    {
        IB::launchJob([](IB::Job::Handle /*currentJob*/)
        {
            IB::atomicIncrement(&Counter);

            // Try to hammer the allocator as much as possible.
            void* value = IB::memoryAllocate(1024 * 1024 * 1024, 1024);
            printf("LARGE ");
            IB::memoryFree(value);

            return IB::JobResult::Complete;
        });
    }
    while (Counter != 10) {}

    IB::killJobSystem();
}
