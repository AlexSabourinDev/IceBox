
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
    constexpr uint32_t iterations = 1024 * 10;
    for (uint32_t i = 0; i < iterations; i++)
    {
        IB::launchJob([]()
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
        IB::launchJob([]()
        {
            for (uint32_t i = 0; i < 10; i++)
            {
                IB::launchJob([i]()
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
        IB::JobHandle sleepJob = IB::launchJob([]()
        {
            Sleep(100);
            return IB::JobResult::Complete;
        });
        IB::JobHandle continueJob1 = IB::continueJob([]()
        {
            Sleep(10);
            return IB::JobResult::Complete;
        }, &sleepJob, 1);
        IB::JobHandle continueJob2 = IB::continueJob([]()
        {
            Sleep(10);
            return IB::JobResult::Complete;
        }, &sleepJob, 1);

        Counter = 0;
        IB::continueJob([]()
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }, &continueJob1, 1);

        IB::continueJob([]()
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }, &continueJob2, 1);

        while (Counter != 2) {}
    }

    for (uint32_t i = 0; i < 10; i++)
    {
        IB::JobHandle sleepJob = IB::launchJob([]()
        {
            Sleep(100);
            return IB::JobResult::Complete;
        });

        IB::JobHandle continueJobs[10];
        for (uint32_t j = 0; j < 10; j++)
        {
            continueJobs[j] = IB::continueJob([]()
            {
                Sleep(10);
                return IB::JobResult::Complete;
            }, &sleepJob, 1);
        }

        Counter = 0;
        IB::continueJob([]()
        {
            IB::atomicIncrement(&Counter);
            return IB::JobResult::Complete;
        }, continueJobs, 10);

        while (Counter != 1) {}
    }

    IB::JobHandle myHandle;
    myHandle = IB::reserveJob([&myHandle]()
    {
        if (Counter == 0)
        {
            IB::JobHandle childJob = IB::launchJob([]()
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
        IB::launchJob([]()
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
        IB::launchJob([]()
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
        IB::launchJob([]()
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
