#pragma once

#include "IBEngineAPI.h"
#include "IBCollection.h"
#include "IBMath.h"
#include <stdint.h>

namespace IB
{
    namespace Asset
    {
        enum class FourCC : uint32_t;
        FourCC constexpr toFourCC(char const (&text)[5]);
    }

    namespace Property
    {
        enum class Handle : uint64_t;
    }

    namespace Entity
    {
        enum class Handle : uint64_t;
    }

    namespace Job
    {
        enum class Handle : uint64_t;
    }


    // Geometry swept accross a line
    struct LineSweepDesc
    {
        DynamicArray<Transform> Points;
        Float2 ShapeDimension = {};
    };

    Asset::FourCC const LineSweepPropertyType = Asset::toFourCC("LNSW");

    IB_API LineSweepDesc* getLineSweep(Property::Handle lineSweepProperty);
    IB_API void updateLineSweep(Entity::Handle entity, LineSweepDesc const& lineSweep);

    IB_API void initGeometryToolkit();
    IB_API void killGeometryToolkit();
}
