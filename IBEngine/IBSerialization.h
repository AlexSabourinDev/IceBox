#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"
#include "Platform/IBPlatform.h"

namespace IB
{
    namespace Serialization
    {
        struct IB_API FileStream
        {
            FileStream(IB::File file) : File(file) {};

            IB::File File;
            static constexpr uint32_t BufferSize = 4096;
            uint8_t Buffer[BufferSize];
            uint32_t BufferCursor = 0;
        };

        IB_API void toBinary(FileStream *stream, void *data, size_t size);
        IB_API void flush(FileStream *stream);

        template <typename T>
        void toBinary(FileStream *stream, T value)
        {
            toBinary(stream, &value, sizeof(T));
        }

        struct IB_API MemoryStream
        {
            MemoryStream() = default;
            MemoryStream(uint8_t *memory) : Memory(memory) {}
            uint8_t *Memory = nullptr;
        };

        IB_API void fromBinary(MemoryStream *stream, void *data, size_t size);
        IB_API void *fromBinary(MemoryStream *stream, size_t size);
        IB_API void advance(MemoryStream* stream, size_t size);

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
    }
} // namespace IB
