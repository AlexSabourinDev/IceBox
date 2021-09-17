#pragma once

#include <stdint.h>
#include <math.h>
#include <float.h>

namespace IB
{
    constexpr float Pi = 3.1415926535f;
    constexpr float Tao = 2.0f * Pi;
    constexpr float DegToRad = Pi / 180.0f;
    constexpr float RadToDeg = 180.0f / Pi;

    template<typename T>
    inline T min(T left, T right) { return left < right ? left : right; }

    struct Float3
    {
        float &operator[](uint32_t i) { return (&x)[i]; }
        float operator[](uint32_t i) const { return (&x)[i]; }
        Float3& operator+=(Float3 rhs);
        Float3 operator-() const { return Float3{ -x, -y, -z }; }

        float x, y, z;
    };

    inline bool operator!=(Float3 const& lhs, Float3 const& rhs)
    {
        return lhs.x != rhs.x
            || lhs.y != rhs.y
            || lhs.z != rhs.z;
    }

    struct AABB
    {
        Float3 Min, Max;
    };

    struct Float4
    {
        float &operator[](uint32_t i) { return (&x)[i]; }
        float operator[](uint32_t i) const { return (&x)[i]; }
        float x, y, z, w;
    };

    struct Quaternion
    {
        float &operator[](uint32_t i) { return (&x)[i]; }
        float operator[](uint32_t i) const { return (&x)[i]; }
        float x, y, z, w;

        static constexpr Quaternion identity() { return Quaternion{ 0.0f, 0.0f, 0.0f, 1.0f }; }
    };

    inline bool operator!=(Quaternion const& lhs, Quaternion const& rhs)
    {
        return lhs.x != rhs.x
            || lhs.y != rhs.y
            || lhs.z != rhs.z
            || lhs.w != rhs.w;
    }

    struct Mat4x4
    {
        Float4 &operator[](uint32_t i) { return Values[i]; }
        Float4 operator[](uint32_t i) const { return Values[i]; }
        Float4 Values[4];

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
        Float4 &operator[](uint32_t i) { return Values[i]; }
        Float4 operator[](uint32_t i) const { return Values[i]; }
        Float4 Values[3];

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

    struct Mat3x3
    {
        Float3 &operator[](uint32_t i) { return Values[i]; }
        Float3 operator[](uint32_t i) const { return Values[i]; }
        Float3 Values[3];

        static constexpr Mat3x3 identity()
        {
            return Mat3x3{
                {
                    {1.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f},
                },
            };
        }
    };

    struct Transform
    {
        Float3 Position = {};
        Quaternion Rotation = Quaternion::identity();
        float Scale = 1.0f;
    };

    struct Triangle
    {
        Float3 Vertices[3];
    };

    inline bool operator!=(Transform const& lhs, Transform const& rhs)
    {
        return lhs.Position != rhs.Position || lhs.Rotation != rhs.Rotation || lhs.Scale != rhs.Scale;
    }

    inline float rcp(float value) { return 1.0f / value; }

    // Right will be applied first, left will be applied second
    // When multiplying a vector by this new matrix
    inline Mat3x4 toMat3x4(Mat3x3 const& matrix, Float3 column);
    inline Mat4x4 toMat4x4(Mat3x4 const& matrix);
    inline Mat4x4 mul(Mat4x4 const&left, Mat4x4 const&right);
    inline Mat4x4 mul(Mat4x4 const&left, Mat3x4 const&right);
    inline Mat4x4 mul(Mat3x4 const&left, Mat4x4 const&right);

    inline Float4 mul(const Mat4x4 &left, Float4 right);

    inline Float4 operator/(Float4 left, float right);

    inline Float3 operator+(Float3 left, Float3 right);
    inline Float3 operator-(Float3 left, Float3 right);
    inline Float3 operator/(Float3 left, float right);
    inline Float3 operator*(Float3 left, float right);
    inline Float3 operator*(float left, Float3 right);
    inline Float3 rcp(Float3 value);
    inline Float3 mul(Float3 left, Float3 right);
    inline Float3 min(Float3 left, Float3 right);
    inline Float3 max(Float3 left, Float3 right);
    inline float length(Float3 value);
    inline float dot(Float3 left, Float3 right);
    inline float* toArray(Float3* value) { return &value->x; };
    inline Float3 mul(const Mat3x3 &left, Float3 right);
    inline Float3 cross(Float3 left, Float3 right);
    inline Float3 normalize(Float3 value);

    inline Float3 rotate(Quaternion rotator, Float3 point);
    inline Quaternion operator*(Quaternion left, Quaternion right);
    inline Quaternion conjugate(Quaternion q);
    inline Quaternion inverse(Quaternion q) { return conjugate(q); }
    inline Quaternion fromAxisAngle(Float3 axis, float angle);
    inline Quaternion fromEulerAngles(Float3 angles);

    inline bool doesLineAABBIntersect(Float3 lineStart, Float3 lineEnd, AABB aabb);
    inline bool doesLineCylinderIntersect(Float3 lineStart, Float3 lineEnd, Float3 cylinderStart, Float3 cylinderEnd, float radius);
    inline Float3 intersectRayPlane(IB::Float3 planeNormal, float planeDistance, IB::Float3 rayDir, IB::Float3 rayOrigin);
    inline AABB consume(AABB aabb, Float3 p);

    inline Mat3x4 toMatrix(const Transform& transform);
    inline Mat3x4 toInvMatrix(const Transform& transform);
    inline Float3 transformDirection(const Transform& transform, Float3 direction);
    inline Float3 inverseTransformDirection(const Transform& transform, Float3 direction);

    inline Float3 transformPoint(const Transform& transform, Float3 direction);
    inline Float3 inverseTransformPoint(const Transform& transform, Float3 direction);

    inline bool doesRayIntersectTriangle(Float3 rayO, Float3 rayD, float rayMin, float rayMax, const Triangle& triangle);

    // Implementation
    inline Mat4x4 toMat4x4(Mat3x4 const& matrix)
    {
        return Mat4x4{ matrix[0], matrix[1], matrix[2], {0.0f, 0.0f, 0.0f, 1.0f} };
    }

    inline Mat3x4 toMat3x4(Mat3x3 const& matrix, Float3 column)
    {
        return Mat3x4{ {
            {matrix[0].x, matrix[0].y, matrix[0].z, column.x},
            {matrix[1].x, matrix[1].y, matrix[1].z, column.y},
            {matrix[2].x, matrix[2].y, matrix[2].z, column.z}
        } };
    }

    inline Mat4x4 mul(const Mat4x4 &left, const Mat4x4 &right)
    {
        Mat4x4 out = {};

        for (uint32_t row = 0; row < 4; row++)
        {
            Float4 r = left[row];

            float results[4][4];
            for (uint32_t i = 0; i < 4; i++)
            {
                results[i][0] = r[i] * right[i][0];
                results[i][1] = r[i] * right[i][1];
                results[i][2] = r[i] * right[i][2];
                results[i][3] = r[i] * right[i][3];
            }

            for (uint32_t i = 0; i < 4; i++)
            {
                out[row][0] += results[i][0];
                out[row][1] += results[i][1];
                out[row][2] += results[i][2];
                out[row][3] += results[i][3];
            }
        }

        return out;
    }

    inline Mat4x4 mul(const Mat4x4 &left, const Mat3x4 &right)
    {
        return mul(left, toMat4x4(right));
    }

    inline Mat4x4 mul(const Mat3x4 &left, const Mat4x4 &right)
    {
        return mul(toMat4x4(left), right);
    }

    inline Float4 mul(const Mat4x4 &left, Float4 right)
    {
        Float4 out = {};
        for (uint32_t row = 0; row < 4; row++)
        {
            Float4 r = {
                left[row][0] * right[0],
                left[row][1] * right[1],
                left[row][2] * right[2],
                left[row][3] * right[3] };
            out[row] = r[0] + r[1] + r[2] + r[3];
        }
        return out;
    }

    inline Float4 operator/(Float4 left, float right)
    {
        return Float4{ left.x / right, left.y / right, left.z / right, left.w / right };
    }

    inline Float3& Float3::operator+=(Float3 rhs)
    {
        *this = *this + rhs;
        return *this;
    }

    inline Float3 operator+(Float3 left, Float3 right)
    {
        return Float3{ left.x + right.x, left.y + right.y, left.z + right.z };
    }

    inline Float3 operator-(Float3 left, Float3 right)
    {
        return Float3{ left.x - right.x, left.y - right.y, left.z - right.z };
    }

    inline Float3 operator/(Float3 left, float right)
    {
        return Float3{ left.x / right, left.y / right, left.z / right };
    }

    inline Float3 operator*(Float3 left, float right)
    {
        return Float3{ left.x * right, left.y * right, left.z * right };
    }

    inline Float3 operator*(float left, Float3 right)
    {
        return right * left;
    }

    inline Float3 rcp(Float3 value)
    {
        return Float3{ 1.0f / value.x, 1.0f / value.y, 1.0f / value.z };
    }

    inline Float3 mul(Float3 left, Float3 right)
    {
        return Float3{ left.x * right.x, left.y * right.y, left.z * right.z };
    }

    inline Float3 min(Float3 left, Float3 right)
    {
        return Float3{ fminf(left.x, right.x), fminf(left.y, right.y), fminf(left.z, right.z) };
    }

    inline Float3 max(Float3 left, Float3 right)
    {
        return Float3{ fmaxf(left.x, right.x), fmaxf(left.y, right.y), fmaxf(left.z, right.z) };
    }

    inline float length(Float3 value)
    {
        return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
    }

    inline float dot(Float3 left, Float3 right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    inline Float3 mul(const Mat3x3 &left, Float3 right)
    {
        Float3 out = {};
        for (uint32_t row = 0; row < 3; row++)
        {
            Float3 r = {
                left[row][0] * right[0],
                left[row][1] * right[1],
                left[row][2] * right[2] };
            out[row] = r[0] + r[1] + r[2];
        }
        return out;
    }

    inline Float3 cross(Float3 left, Float3 right)
    {
        return Float3
        {
            left.y*right.z - left.z*right.y,
            left.z*right.x - left.x*right.z,
            left.x*right.y - left.y*right.x
        };
    }

    inline Float3 normalize(Float3 value)
    {
        float l = length(value);
        if (l == 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }

        return value / l;
    }

    inline Float3 rotate(Quaternion rotator, Float3 point)
    {
        Quaternion rotatedPoint = rotator * Quaternion{ point.x, point.y, point.z, 0.0f } *conjugate(rotator);
        return Float3{ rotatedPoint.x, rotatedPoint.y, rotatedPoint.z };
    }

    inline Quaternion operator*(Quaternion left, Quaternion right)
    {
        return Quaternion
        {
            left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
            left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
            left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
            left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z
        };
    }

    inline Quaternion conjugate(Quaternion q)
    {
        return Quaternion{ -q.x, -q.y, -q.z, q.w };
    }

    inline Quaternion fromAxisAngle(Float3 axis, float angle)
    {
        Float3 real = axis * sinf(angle * 0.5f);
        return Quaternion{ real.x, real.y, real.z, cosf(angle * 0.5f) };
    }

    inline Quaternion fromEulerAngles(Float3 angles)
    {
        return fromAxisAngle(Float3{ 1.0f, 0.0f, 0.0f }, angles.x)
            * fromAxisAngle(Float3{ 0.0f, 1.0f, 0.0f }, angles.y)
            * fromAxisAngle(Float3{ 0.0f, 0.0f, 1.0f }, angles.z);
    }

    inline bool doesLineAABBIntersect(Float3 lineStart, Float3 lineEnd, AABB aabb)
    {
        // Source: https://medium.com/@bromanz/another-view-on-the-classic-ray-aabb-intersection-algorithm-for-bvh-traversal-41125138b525

        Float3 delta = lineEnd - lineStart;

        Float3 invD = rcp(delta);
        Float3 t0s = mul(aabb.Min - lineStart, invD);
        Float3 t1s = mul(aabb.Max - lineStart, invD);

        Float3 tsmaller = min(t0s, t1s);
        Float3 tbigger = max(t0s, t1s);

        float tmin = fmaxf(0.0f, fmaxf(tsmaller.x, fmaxf(tsmaller.y, tsmaller.z)));
        float tmax = fminf(1.0f, fminf(tbigger.x, fminf(tbigger.y, tbigger.z)));
        return (tmin < tmax);
    }

    inline AABB consume(AABB aabb, Float3 p)
    {
        return AABB{ min(aabb.Min, p), max(aabb.Max, p) };
    }

    inline bool doesLineCylinderIntersect(Float3 lineStart, Float3 lineEnd, Float3 cylinderStart, Float3 cylinderEnd, float radius)
    {
        Float3 delta = cylinderEnd - cylinderStart;

        Float3 s = lineStart - cylinderStart;
        Float3 e = lineEnd - cylinderStart;

        // Make our line go in the direction of our cylinder
        if (dot(delta, e - s) < 0.0f)
        {
            Float3 t = s;
            s = e;
            e = t;
        }

        // If our line on either side of our cylinder's cap planes?
        float startDot = dot(s, delta);
        float endDot = dot(e, delta);
        if (startDot < 0.0f && endDot < 0.0f || startDot > 1.0f && endDot > 1.0f)
        {
            return false;
        }

        {
            // Find t where our line intersects a plane
            // ((e-s)*t+s).d=f
            // (e-s)*t.d+s.d=f
            // (e-s)*t.d=f-s.d
            // t*((e-s).d)=f-s.d
            // t=(f-s.d)/((e-s).d)

            auto clipToPlane = [](Float3 s, Float3 e, Float3 n, float d)
            {
                float t = (d - dot(s, n)) / dot(e - s, n);
                t = fminf(fmaxf(t, 0.0f), 1.0f);
                return (e - s)*t + s;
            };

            s = clipToPlane(s, e, delta, 0.0f); // start plane
            e = clipToPlane(s, e, delta, 1.0f); // end plane
        }

        // Project our line against the plane defined by our cylinder caps
        // Then calculate the closest point this new projected line has to 0,0,0
        s = s - dot(s, delta) * delta;
        e = e - dot(e, delta) * delta;

        // To calculate the closest point for a line to a point, we can calculate the local minimum using the derivative
        // of our equation where:
        // ||((e-s)*t+s)-p||=distance
        // since p is 0, 0, 0
        // ||(e-s)*t+s||=distance
        // break into components
        // sqrt(((e.x-s.x)*t+s.x)^2+...+)=distance
        // ((e.x-s.x)*t+s.x)^2+...+=distance^2
        // d = distance^2
        // ((e.x-s.x)*t+s.x)^2+...+=d
        // calculate the derivative to find when dd/dt is 0
        // 2((e.x-s.x)*t+s.x)*(e.x-s.x)+...+=dd/dt
        // when dd/dt is 0
        // 2((e.x-s.x)*t+s.x)*(e.x-s.x)+...+=0
        // ((e.x-s.x)*t+s.x)*(e.x-s.x)+...+=0
        // (e.x-s.x)*t*(e.x-s.x)+s.x*(e.x-s.x)+...+=0
        // (e.x-s.x)*t*(e.x-s.x)+...+=-s.x*(e.x-s.x)-...-
        // (e.x-s.x)^2*t+...+=-s.x*(e.x-s.x)-...-
        // (e-s).(e-s)*t=-s.(e-s)
        // t=-s.(e-s)/((e-s).(e-s))
        float d = dot(e - s, e - s);

        // If our denominator is 0 that means we're a point,
        // we can just set t to 1
        // since the equation will give us the point's position anyways
        float t = d != 0.0f ? -dot(s, e - s) / d : 1.0f;
        if (t < 0.0f || t > 1.0f)
        {
            return false;
        }

        // Find our point and test it's distance
        return dot((e - s)*t + s, (e - s)*t + s) <= radius * radius;
    }

    inline Float3 intersectRayPlane(IB::Float3 planeNormal, float planeDistance, IB::Float3 rayDir, IB::Float3 rayOrigin)
    {
        // Equation for plane:
        // 0=(p.n)-d
        // Equation for ray
        // p=f*t+o
        // Plug ray into plane
        // 0=((f*t+o).n)-d
        // d=(f*t+o).n
        // d=f.n*t+o.n
        // d-o.n=f.n*t
        // (d-o.n)/(f.n)=t

        float t = (planeDistance - dot(rayOrigin, planeNormal)) / dot(rayDir, planeNormal);
        return rayDir * t + rayOrigin;
    };

    inline Mat3x4 toMatrix(const Transform& transform)
    {
        Float3 right = rotate(transform.Rotation, Float3{ transform.Scale, 0.0f, 0.0f });
        Float3 forward = rotate(transform.Rotation, Float3{ 0.0f, transform.Scale, 0.0f });
        Float3 up = rotate(transform.Rotation, Float3{ 0.0f, 0.0f, transform.Scale });
        return Mat3x4
        {
            right.x, forward.x, up.x, transform.Position.x,
            right.y, forward.y, up.y, transform.Position.y,
            right.z, forward.z, up.z, transform.Position.z,
        };
    }

    inline Mat3x4 toInvMatrix(const Transform& transform)
    {
        // Given that our original transformation is:
        // P1 = P0 * S * R + T
        // Our inverse should be
        // (P1 - T) invR * invS
        // Which we can distribute as
        // P1 * invR * invS  - T * invR * invS

        Float3 right = rotate(transform.Rotation, Float3{ 1.0f / transform.Scale, 0.0f, 0.0f });
        Float3 forward = rotate(transform.Rotation, Float3{ 0.0f, 1.0f / transform.Scale, 0.0f });
        Float3 up = rotate(transform.Rotation, Float3{ 0.0f, 0.0f, 1.0f / transform.Scale });

        Mat3x3 inverseScaleRotation =
        {
            right.x, right.y, right.z,
            forward.x, forward.y, forward.z,
            up.x, up.y, up.z
        };

        Float3 inversePosition = mul(inverseScaleRotation, transform.Position);
        return toMat3x4(inverseScaleRotation, -inversePosition);
    }

    inline Float3 transformDirection(const Transform& transform, Float3 direction)
    {
        return rotate(transform.Rotation, direction * transform.Scale);
    }

    inline Float3 inverseTransformDirection(const Transform& transform, Float3 direction)
    {
        return rotate(inverse(transform.Rotation), direction) / transform.Scale;
    }

    inline Float3 transformPoint(const Transform& transform, Float3 point)
    {
        return rotate(transform.Rotation, point * transform.Scale) + transform.Position;
    }

    inline Float3 inverseTransformPoint(const Transform& transform, Float3 point)
    {
        return rotate(inverse(transform.Rotation), point - transform.Position) / transform.Scale;
    }

    inline bool doesRayIntersectTriangle(Float3 rayO, Float3 rayD, float rayMin, float rayMax, Triangle triangle)
    {
        // Source: https://www.scratchapixel.com/lessons/3d-basic-rendering/ray-tracing-rendering-a-triangle/moller-trumbore-ray-triangle-intersection
        Float3 e1 = triangle.Vertices[1] - triangle.Vertices[0];
        Float3 e2 = triangle.Vertices[2] - triangle.Vertices[0];
        Float3 p = cross(rayD, e2);
        float d = dot(e1, p);
        if (d < FLT_EPSILON)
        {
            return false;
        }

        float invD = rcp(d);
        Float3 tv = rayO - triangle.Vertices[0];
        float v = dot(tv, p) * invD;
        if (v < 0.0f || v > 1.0f)
        {
            return false;
        }

        Float3 q = cross(tv, e1);
        float w = dot(rayD, q) * invD;
        if (w < 0.0f || v + w > 1.0f)
        {
            return false;
        }

        float t = dot(e2, q) * invD;
        if (t > rayMin && t < rayMax)
        {
            return true;
        }

        return false;
    }
}

#include <immintrin.h>
namespace IB
{
    // Simd API
    constexpr uint32_t SimdLaneCount = 4;

    struct alignas(16) SimdFloat
    {
        __m128 Values;
    };

    struct SimdFloat3
    {
        SimdFloat x;
        SimdFloat y;
        SimdFloat z;
    };

    struct SimdTriangleSet
    {
        SimdFloat3 Vertices[3];
    };

    inline SimdFloat splat(float value);
    inline SimdFloat splatBitwise(int32_t value);
    inline SimdFloat simdSet(float const (&values)[SimdLaneCount]);
    inline SimdFloat operator+(SimdFloat left, SimdFloat right);
    inline SimdFloat operator-(SimdFloat left, SimdFloat right);
    inline SimdFloat operator*(SimdFloat left, SimdFloat right);
    inline SimdFloat lessThan(SimdFloat left, SimdFloat right);
    inline SimdFloat lessThanOrEqual(SimdFloat left, SimdFloat right);
    inline SimdFloat greaterThan(SimdFloat left, SimdFloat right);
    inline SimdFloat greaterThanOrEqual(SimdFloat left, SimdFloat right);
    inline SimdFloat bitOr(SimdFloat left, SimdFloat right);
    inline SimdFloat bitAnd(SimdFloat left, SimdFloat right);
    inline SimdFloat rcp(SimdFloat value);
    inline SimdFloat abs(SimdFloat value);
    inline SimdFloat bitNot(SimdFloat value);
    inline bool any(SimdFloat comparison, uint32_t mask = 0xFFFFFFFF);
    inline bool all(SimdFloat comparison, uint32_t mask = 0xFFFFFFFF);

    inline SimdFloat3 splat(Float3 value);
    // valueCount must be less than or equal to 4
    inline SimdFloat3 simdSet(Float3 (&values)[SimdLaneCount]);
    inline SimdFloat3 operator-(const SimdFloat3& left, const SimdFloat3& right);
    inline SimdFloat3 cross(const SimdFloat3& left, const SimdFloat3& right);
    inline SimdFloat dot(const SimdFloat3& left, const SimdFloat3& right);

    inline bool doesRayIntersectAnyTriangle(SimdFloat3 rayO, SimdFloat3 rayD, SimdFloat rayMin, SimdFloat rayMax, const SimdTriangleSet& triangleSet, uint32_t trianglesInSet);

    static const SimdFloat SimdEpsilon = splat(FLT_EPSILON);
    static const SimdFloat SimdZero = splat(0.0f);
    static const SimdFloat SimdOne = splat(1.0f);
    static const SimdFloat SimdAllMask = splatBitwise(-1); // Create a mask of all bits set

    // Implementation

    inline SimdFloat splat(float value) { return SimdFloat{ _mm_set1_ps(value) }; }
    inline SimdFloat splatBitwise(int32_t value) { return SimdFloat{ _mm_castsi128_ps(_mm_set1_epi32(value)) }; }

    inline SimdFloat simdSet(float const (&values)[SimdLaneCount]) { return SimdFloat{ _mm_set_ps(values[0], values[1], values[2], values[3]) }; }
    inline SimdFloat operator+(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_add_ps(left.Values, right.Values) }; }
    inline SimdFloat operator-(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_sub_ps(left.Values, right.Values) }; }
    inline SimdFloat operator*(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_mul_ps(left.Values, right.Values) }; }
    inline SimdFloat lessThan(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_cmp_ps(left.Values, right.Values, _CMP_LT_OQ) }; }
    inline SimdFloat lessThanOrEqual(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_cmp_ps(left.Values, right.Values, _CMP_LE_OQ) }; }
    inline SimdFloat greaterThan(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_cmp_ps(left.Values, right.Values, _CMP_GT_OQ) }; }
    inline SimdFloat greaterThanOrEqual(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_cmp_ps(left.Values, right.Values, _CMP_GE_OQ) }; }
    inline SimdFloat bitOr(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_or_ps(left.Values, right.Values) }; };
    inline SimdFloat bitAnd(SimdFloat left, SimdFloat right) { return SimdFloat{ _mm_and_ps(left.Values, right.Values) }; };
    inline SimdFloat rcp(SimdFloat value) { return SimdFloat{ _mm_rcp_ps(value.Values) }; }
    inline SimdFloat abs(SimdFloat value)
    {
        return SimdFloat{ _mm_and_ps(value.Values, splatBitwise(static_cast<int32_t>(0x7FFFFFFF)).Values) }; // Mask out our sign bit
    }
    inline SimdFloat bitNot(SimdFloat value) { return SimdFloat{ _mm_xor_ps(value.Values, SimdAllMask.Values) }; }

    inline bool any(SimdFloat comparison, uint32_t mask)
    {
        return (_mm_movemask_ps(comparison.Values) & mask) != 0;
    }

    inline bool all(SimdFloat comparison, uint32_t mask)
    {
        return (_mm_movemask_ps(comparison.Values) & mask) == mask;
    }

    inline SimdFloat3 splat(Float3 value) { return SimdFloat3{ splat(value.x), splat(value.y), splat(value.z) }; }
    inline SimdFloat3 operator-(const SimdFloat3& left, const SimdFloat3& right) { return SimdFloat3{ left.x-right.x, left.y - right.y, left.z - right.z }; }

    inline SimdFloat3 simdSet(Float3(&values)[SimdLaneCount])
    {
        float x[SimdLaneCount] = {};
        float y[SimdLaneCount] = {};
        float z[SimdLaneCount] = {};
        for (uint32_t i = 0; i < SimdLaneCount; i++)
        {
            x[i] = values[i].x;
            y[i] = values[i].y;
            z[i] = values[i].z;
        }

        return SimdFloat3
        {
            simdSet(x),
            simdSet(y),
            simdSet(z)
        };
    }

    inline SimdFloat3 cross(const SimdFloat3& left, const SimdFloat3& right)
    {
        return SimdFloat3
        {
            left.y*right.z - left.z*right.y,
            left.z*right.x - left.x*right.z,
            left.x*right.y - left.y*right.x
        };
    }

    inline SimdFloat dot(const SimdFloat3& left, const SimdFloat3& right)
    {
        return SimdFloat{ left.x * right.x + left.y * right.y + left.z * right.z };
    }

    inline bool doesRayIntersectAnyTriangle(SimdFloat3 rayO, SimdFloat3 rayD, SimdFloat rayMin, SimdFloat rayMax, const SimdTriangleSet& triangleSet, uint32_t trianglesInSet)
    {
        // Source: https://www.scratchapixel.com/lessons/3d-basic-rendering/ray-tracing-rendering-a-triangle/moller-trumbore-ray-triangle-intersection
        SimdFloat3 e1 = triangleSet.Vertices[1] - triangleSet.Vertices[0];
        SimdFloat3 e2 = triangleSet.Vertices[2] - triangleSet.Vertices[0];
        SimdFloat3 p = cross(rayD, e2);
        SimdFloat d = dot(e1, p);

        uint32_t const triangleMask = (1 << trianglesInSet) - 1;
        SimdFloat projectionTest = greaterThanOrEqual(d, SimdEpsilon);
        SimdFloat resultMask = projectionTest;
        // If none of our lanes pass, early out
        if (all(bitNot(resultMask), triangleMask))
        {
            return false;
        }

        SimdFloat invD = rcp(d);
        SimdFloat3 tv = rayO - triangleSet.Vertices[0];
        SimdFloat v = dot(tv, p) * invD;

        projectionTest = bitAnd(greaterThanOrEqual(v, SimdZero), lessThanOrEqual(v, SimdOne));
        resultMask = bitAnd(resultMask, projectionTest); // Carry our successes forward

        // If all of our triangles fail, then end early
        if (all(bitNot(resultMask), triangleMask))
        {
            return false;
        }

        SimdFloat3 q = cross(tv, e1);
        SimdFloat w = dot(rayD, q) * invD;

        // If all of our triangles fail, then end early

        projectionTest = bitAnd(greaterThanOrEqual(w, SimdZero), lessThanOrEqual(v + w, SimdOne));
        resultMask = bitAnd(resultMask, projectionTest); // Carry our successes forward

        // If all of our triangles fail, then end early
        if (all(bitNot(resultMask), triangleMask))
        {
            return false;
        }

        SimdFloat t = dot(e2, q) * invD;
        projectionTest = bitAnd(greaterThan(t, rayMin), lessThan(t, rayMax));
        resultMask = bitAnd(projectionTest, resultMask);

        // If any of our triangles have made it this far, pass
        if (any(resultMask, triangleMask))
        {
            return true;
        }

        return false;
    }
} // namespace IB
