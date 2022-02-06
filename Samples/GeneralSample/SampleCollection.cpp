#include <IBEngine/IBCollection.h>
#include <IBEngine/IBLogging.h>

uint32_t hash(char const *path)
{
    uint32_t hash = 5381;
    for (; *path != 0; path++)
    {
        hash = hash * 33 + *path;
    }

    return hash;
}

uint64_t keyHash(char const *const &key)
{
    return hash(key);
}


void sampleCollection()
{
    // Linear probe hashmap test
    {
        IB::LinearProbeHashMap<char const *, uint32_t, keyHash> keyValues;
        keyValues.set("set", 0);
        IB_ASSERT(keyValues.isSet("set"), "Key should be set!");
        IB_ASSERT(!keyValues.isSet("not set"), "Key should not be set!");
        uint32_t value;
        IB_ASSERT(keyValues.tryGet("set", &value) && value == 0, "Value not correct!");

        keyValues.set("set2", 1);
        IB_ASSERT(keyValues.isSet("set"), "Key should be set!");
        IB_ASSERT(keyValues.isSet("set2"), "Key should be set!");

        IB_ASSERT(keyValues.tryGet("set", &value) && value == 0, "Value not correct!");
        IB_ASSERT(keyValues.tryGet("set2", &value) && value == 1, "Value not correct!");
        IB_ASSERT(!keyValues.tryGet("set3", &value), "Value not correct!");

        IB_ASSERT(!keyValues.isSet("not set"), "Key should not be set!");
        IB_ASSERT(!keyValues.isSet("not set2"), "Key should not be set!");

        keyValues.clear("set");
        IB_ASSERT(!keyValues.isSet("set"), "Key should not be set!");
        IB_ASSERT(keyValues.isSet("set2"), "Key should be set!");
        IB_ASSERT(!keyValues.tryGet("set", &value), "Value not correct!");

        IB_ASSERT(keyValues.tryGet("set2", &value) && value == 1, "Value not correct!");

        keyValues.clear("set2");

        for (uint32_t i = 0; i < 26; i++)
        {
            char c = 'a' + i;
            keyValues.set(&c, c);
            IB_ASSERT(keyValues.isSet(&c), "Key should be set!");
            IB_ASSERT(keyValues.tryGet(&c, &value) && value == c, "Key should be set!");

            c++;
            IB_ASSERT(!keyValues.isSet(&c), "Key should not be set!");
            IB_ASSERT(!keyValues.tryGet(&c, &value), "Key should not be set!");
        }

        for (uint32_t i = 0; i < 26; i++)
        {
            char c = 'A' + i;
            keyValues.set(&c, c);
            IB_ASSERT(keyValues.isSet(&c), "Key should be set!");
            IB_ASSERT(keyValues.tryGet(&c, &value) && value == c, "Key should be set!");

            c++;
            IB_ASSERT(!keyValues.isSet(&c), "Key should not be set!");
            IB_ASSERT(!keyValues.tryGet(&c, &value), "Key should not be set!");
        }

        for (uint32_t i = 0; i < 26; i++)
        {
            char c = 'a' + i;
            keyValues.clear(&c);
            IB_ASSERT(!keyValues.isSet(&c), "Key should not be set!");
            IB_ASSERT(!keyValues.tryGet(&c, &value), "Key should not be set!");
        }

        for (uint32_t i = 0; i < 26; i++)
        {
            char c = 'A' + i;
            keyValues.clear(&c);
            IB_ASSERT(!keyValues.isSet(&c), "Key should not be set!");
            IB_ASSERT(!keyValues.tryGet(&c, &value), "Key should not be set!");
        }
    }
}
