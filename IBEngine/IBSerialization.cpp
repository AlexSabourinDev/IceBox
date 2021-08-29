#define _CRT_SECURE_NO_WARNINGS
#include "IBSerialization.h"
#include "IBLogging.h"
#include "IBAllocator.h"

#include <stdint.h>
#include <string.h>

namespace IB
{
    namespace Serialization
    {
        const uint32_t Version = 3;

        void initSerialization()
        {
            // Nothing
        }

        void killSerialization()
        {
            // Nothing
        }

        WriteStream::~WriteStream()
        {
            memoryFree(Buffer);
        }

        MemoryBuffer::~MemoryBuffer()
        {
            memoryFree(Data);
        }

        MemoryBuffer::MemoryBuffer(MemoryBuffer&& other)
        {
            operator=(std::move(other));
        }

        MemoryBuffer& MemoryBuffer::operator=(MemoryBuffer&& other)
        {
            uint8_t* oldData = Data;
            uint32_t oldSize = Size;
            Data = other.Data;
            Size = other.Size;
            other.Data = oldData;
            other.Size = oldSize;

            return *this;
        }

        void toBinary(WriteStream *stream, void const *data, size_t size)
        {
            uint32_t constexpr bufferChunkSize = 4096;
            uint32_t constexpr maxAlignment = 8;
            if (stream->BufferCursor + size > stream->BufferSize)
            {
                uint32_t newChunkSize = (size > bufferChunkSize ? static_cast<uint32_t>(size) : bufferChunkSize);
                uint32_t newSize = stream->BufferSize + newChunkSize;

                uint8_t* newMemory;
                if (stream->Buffer != nullptr)
                {
                    uint8_t* oldMemory = stream->Buffer;
                    newMemory = reinterpret_cast<uint8_t*>(memoryAllocate(newSize, maxAlignment));
                    memcpy(newMemory, oldMemory, stream->BufferSize);
                    memoryFree(oldMemory);
                }
                else
                {
                    newMemory = reinterpret_cast<uint8_t*>(memoryAllocate(newSize, maxAlignment));
                }

                stream->Buffer = newMemory;
                stream->BufferSize += bufferChunkSize;
            }

            memcpy(stream->Buffer + stream->BufferCursor, data, size); // TODO: alignment
            stream->BufferCursor += static_cast<uint32_t>(size);
        }

        void fromBinary(ReadStream *stream, void *data, size_t size)
        {
            memcpy(data, stream->Memory, size);
            stream->Memory += size;
        }

        void const *fromBinary(ReadStream *stream, size_t size)
        {
            void const *memory = stream->Memory;
            stream->Memory += size;
            return memory;
        }

        void advance(ReadStream *stream, size_t size)
        {
            stream->Memory += size;
        }
    } // namespace Serialization
} // namespace IB
