#pragma once

#include "IBEngineAPI.h"
#include "IBCollection.h"
#include "IBMath.h"
#include <stdint.h>

namespace IB
{
    namespace Asset
    {
        enum FourCC : uint32_t;
        constexpr FourCC toFourCC(char const (&text)[5]);
    }

    // Geometry swept accross a line
    struct LineSweep
    {
        DynamicArray<Float3> Points;
        Float2 ShapeDimension = {};
    };

    namespace LineSweepProperty
    {
        using Handle = LineSweep*;
    }
    Asset::FourCC const LineSweepPropertyType = Asset::toFourCC("LNSW");

    IB_API void initGeometryToolkit();
    IB_API void killGeometryToolkit();
}
