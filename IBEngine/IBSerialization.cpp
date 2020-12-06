#define _CRT_SECURE_NO_WARNINGS
#include "IBSerialization.h"
#include "IBPlatform.h"
#include "IBLogging.h"
#include "IBAllocator.h"

#include <stdint.h>
#include <string.h>

namespace IB
{
    namespace Serialization
    {
        void initSerialization()
        {
            // Nothing
        }

        void killSerialization()
        {
            // Nothing
        }

        void toBinary(FileStream *stream, void const *data, size_t size)
        {
            if (stream->BufferCursor + size > FileStream::BufferSize)
            {
                flush(stream);
            }

            if (size > FileStream::BufferSize) // Large writes go direct to file
            {
                appendToFile(stream->File, data, size);
            }
            else
            {
                memcpy(stream->Buffer + stream->BufferCursor, data, size);
                stream->BufferCursor += static_cast<uint32_t>(size);
            }
        }

        uint32_t flush(FileStream *stream)
        {
            appendToFile(stream->File, stream->Buffer, stream->BufferCursor);
            stream->BufferCursor = 0;
            return static_cast<uint32_t>(fileSize(stream->File));
        }

        void fromBinary(MemoryStream *stream, void *data, size_t size)
        {
            memcpy(data, stream->Memory, size);
            stream->Memory += size;
        }

        void const *fromBinary(MemoryStream *stream, size_t size)
        {
            void *memory = stream->Memory;
            stream->Memory += size;
            return memory;
        }

        void advance(MemoryStream *stream, size_t size)
        {
            stream->Memory += size;
        }
    } // namespace Serialization
} // namespace IB
