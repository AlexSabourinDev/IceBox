
#include <IBEngine/IBJobs.h>
#include <IBEngine/Platform/IBPlatform.h>
#include <stdio.h>

IB::AtomicU32 Counter = { 0 };
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
            printf("%u ", Counter.Value);
        });
    }

    while (Counter.Value != iterations) {}

    // Spawn job from job
    Counter.Value = 0;
    for (uint32_t i = 0; i < iterations / 10; i++)
    {
        IB::launchJob([]()
        {
            for (uint32_t i = 0; i < 10; i++)
            {
                IB::launchJob([i]()
                {
                    IB::atomicIncrement(&Counter);
                    printf("%u: %u ", i, Counter.Value);
                });
            }
        });
    }

    while (Counter.Value != iterations) {}
    IB::killJobSystem();
}
