#pragma once

#include <stdint.h>
#include <string.h>

#include "IBEngineAPI.h"

namespace IB
{
    namespace File
    {
        enum class Handle : uintptr_t;
    }

    namespace Serialization
    {
        extern const uint32_t Version;

        IB_API void initSerialization();
        IB_API void killSerialization();

        struct WriteStream
        {
            WriteStream() = default;
            ~WriteStream();
            WriteStream(WriteStream const&) = delete;
            WriteStream& operator=(WriteStream const&) = delete;

            uint8_t* Buffer = nullptr;
            uint32_t BufferCursor = 0;
            uint32_t BufferSize = 0;
        };

        struct MemoryBuffer
        {
            MemoryBuffer() = default;
            ~MemoryBuffer();
            MemoryBuffer(MemoryBuffer&& other);
            MemoryBuffer& operator=(MemoryBuffer&& other);

            uint8_t* Data = nullptr;
            uint32_t Size = 0;
        };

        inline MemoryBuffer steal(WriteStream* stream)
        {
            MemoryBuffer buffer;

            buffer.Data = stream->Buffer;
            buffer.Size = stream->BufferSize;
            stream->Buffer = nullptr;
            stream->BufferSize = 0;
            stream->BufferCursor = 0;

            return buffer;
        };

        IB_API void toBinary(WriteStream *stream, void const *data, size_t size);

        template <typename T>
        void arrayToBinary(WriteStream *stream, T const* data, uint32_t count)
        {
            toBinary(stream, data, count * sizeof(T));
        }

        template <typename T>
        void toBinary(WriteStream *stream, const T& value)
        {
            toBinary(stream, &value, sizeof(T));
        }

        inline void toBinary(WriteStream *stream, char const *string)
        {
            uint32_t stringSize = static_cast<uint32_t>(strlen(string)) + 1;
            toBinary(stream, stringSize);
            toBinary(stream, string, stringSize);
        }

        struct ReadStream
        {
            ReadStream() = default;
            ReadStream(uint8_t const*memory) : Memory(memory) {}
            uint8_t const*Memory = nullptr;
        };

        IB_API void fromBinary(ReadStream *stream, void *data, size_t size);
        IB_API void const *fromBinary(ReadStream *stream, size_t size);
        IB_API void advance(ReadStream *stream, size_t size);

        template <typename T>
        void fromBinary(ReadStream *stream, T *value)
        {
            fromBinary(stream, value, sizeof(T));
        }

        template <typename T>
        T const* arrayFromBinary(ReadStream *stream, uint32_t count)
        {
            return reinterpret_cast<T const*>(fromBinary(stream, count * sizeof(T)));
        }

        inline void fromBinary(ReadStream *stream, char const **string)
        {
            uint32_t stringSize;
            fromBinary(stream, &stringSize);
            *string = arrayFromBinary<char>(stream, stringSize);
        }

        namespace Delta
        {
            template <typename T>
            void toBinary(WriteStream *stream, T const& value, T const& baseValue)
            {
                bool isDifferent = value != baseValue;
                toBinary(stream, isDifferent);
                if (isDifferent)
                {
                    toBinary(stream, value);
                }
            }

            inline void toBinary(WriteStream *stream, char const* value, char const* baseValue)
            {
                bool isDifferent = strcmp(value, baseValue) != 0;
                toBinary(stream, isDifferent);
                if (isDifferent)
                {
                    toBinary(stream, value);
                }
            }

            template <typename T>
            void fromBinary(ReadStream *stream, T *value, T const& baseValue)
            {
                bool isDifferent;
                fromBinary(stream, &isDifferent);
                if (isDifferent)
                {
                    fromBinary(stream, value);
                }
                else
                {
                    *value = baseValue;
                }
            }

            inline void fromBinary(ReadStream *stream, char const **value, char const* baseValue)
            {
                bool isDifferent;
                fromBinary(stream, &isDifferent);
                if (isDifferent)
                {
                    fromBinary(stream, value);
                }
                else
                {
                    *value = baseValue;
                }
            }
        }
    } // namespace Serialization
} // namespace IB
