#pragma once

#include <stdint.h>

namespace IB
{
    struct Mat4x4
    {
        using Collumn = float (&)[4];
        Collumn operator[](uint32_t i) { return Values[i]; }
        float Values[4][4];

        static constexpr Mat4x4 identity()
        {
            return Mat4x4{
                {
                    {1.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f, 1.0f},
                },
            };
        }
    };

    struct Mat3x4
    {
        using Collumn = float (&)[4];
        Collumn operator[](uint32_t i) { return Values[i]; }
        float Values[3][4];

        static constexpr Mat3x4 identity()
        {
            return Mat3x4{
                {
                    {1.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f, 0.0f},
                },
            };
        }
    };
} // namespace IB
