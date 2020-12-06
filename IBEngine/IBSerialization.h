#pragma once

#include <stdint.h>
#include <string.h>

#include "IBEngineAPI.h"
#include "IBPlatform.h"

namespace IB
{
    namespace Serialization
    {
        IB_API void initSerialization();
        IB_API void killSerialization();

        struct FileStream
        {
            FileStream(IB::File file) : File(file){};

            IB::File File;
            static constexpr uint32_t BufferSize = 4096;
            uint8_t Buffer[BufferSize];
            uint32_t BufferCursor = 0;
        };

        IB_API void toBinary(FileStream *stream, void const *data, size_t size);
        IB_API uint32_t flush(FileStream *stream);

        template <typename T>
        void toBinary(FileStream *stream, T value)
        {
            toBinary(stream, &value, sizeof(T));
        }

        inline void toBinary(FileStream *stream, char const *string)
        {
            uint32_t stringSize = static_cast<uint32_t>(strlen(string)) + 1;
            toBinary(stream, stringSize);
            toBinary(stream, string, stringSize);
        }

        struct MemoryStream
        {
            MemoryStream() = default;
            MemoryStream(uint8_t *memory) : Memory(memory) {}
            uint8_t *Memory = nullptr;
        };

        IB_API void fromBinary(MemoryStream *stream, void *data, size_t size);
        IB_API void const *fromBinary(MemoryStream *stream, size_t size);
        IB_API void advance(MemoryStream *stream, size_t size);

        template <typename T>
        void fromBinary(MemoryStream *stream, T *value)
        {
            fromBinary(stream, value, sizeof(T));
        }

        template <typename T>
        T fromBinary(MemoryStream *stream, size_t size)
        {
            return reinterpret_cast<T>(fromBinary(stream, size));
        }

        inline void fromBinary(MemoryStream *stream, char const **string)
        {
            uint32_t stringSize;
            fromBinary(stream, &stringSize);
            *string = fromBinary<char const *>(stream, stringSize);
        }
    } // namespace Serialization
} // namespace IB
