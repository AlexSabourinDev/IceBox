#pragma once

#include <stdint.h>
#include <math.h>

namespace IB
{
    constexpr float Pi = 3.1415926535f;
    constexpr float Tao = 2.0f * Pi;

    struct Float3
    {
        float &operator[](uint32_t i) { return (&x)[i]; }
        float operator[](uint32_t i) const { return (&x)[i]; }
        float x, y, z;
    };

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

    // Left will be applied first, right will be applied second
    // When multiplying a vector by this new matrix
    inline Mat4x4 mul(const Mat4x4 &left, const Mat4x4 &right);
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

    inline bool doesLineAABBIntersect(Float3 lineStart, Float3 lineEnd, AABB aabb);
    inline bool doesLineCylinderIntersect(Float3 lineStart, Float3 lineEnd, Float3 cylinderStart, Float3 cylinderEnd, float radius);
    inline Float3 intersectRayPlane(IB::Float3 planeNormal, float planeDistance, IB::Float3 rayDir, IB::Float3 rayOrigin);
    inline AABB consume(AABB aabb, Float3 p);

    // Implementation
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

    inline Float4 mul(const Mat4x4 &left, Float4 right)
    {
        Float4 out = {};
        for (uint32_t row = 0; row < 4; row++)
        {
            Float4 r = {
                left[row][0] * right[0],
                left[row][1] * right[1],
                left[row][2] * right[2],
                left[row][3] * right[3]};
            out[row] = r[0] + r[1] + r[2] + r[3];
        }
        return out;
    }

    inline Float4 operator/(Float4 left, float right)
    {
        return Float4{left.x / right, left.y / right, left.z / right, left.w / right};
    }

    inline Float3 operator+(Float3 left, Float3 right)
    {
        return Float3{ left.x + right.x, left.y + right.y, left.z + right.z };
    }

    inline Float3 operator-(Float3 left, Float3 right)
    {
        return Float3{left.x - right.x, left.y - right.y, left.z - right.z};
    }

    inline Float3 operator/(Float3 left, float right)
    {
        return Float3{left.x / right, left.y / right, left.z / right};
    }

    inline Float3 operator*(Float3 left, float right)
    {
        return Float3{left.x * right, left.y * right, left.z * right};
    }

    inline Float3 operator*(float left, Float3 right)
    {
        return right * left;
    }

    inline Float3 rcp(Float3 value)
    {
        return Float3{1.0f / value.x, 1.0f / value.y, 1.0f / value.z};
    }

    inline Float3 mul(Float3 left, Float3 right)
    {
        return Float3{left.x * right.x, left.y * right.y, left.z * right.z};
    }

    inline Float3 min(Float3 left, Float3 right)
    {
        return Float3{fminf(left.x, right.x), fminf(left.y, right.y), fminf(left.z, right.z)};
    }

    inline Float3 max(Float3 left, Float3 right)
    {
        return Float3{fmaxf(left.x, right.x), fmaxf(left.y, right.y), fmaxf(left.z, right.z)};
    }

    inline float length(Float3 value)
    {
        return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
    }

    inline float dot(Float3 left, Float3 right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
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
        return AABB{min(aabb.Min, p), max(aabb.Max, p)};
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
                float t = (d-dot(s, n)) / dot(e - s, n);
                t = fminf(fmaxf(t, 0.0f), 1.0f);
                return (e - s)*t + s;
            };

            s = clipToPlane(s, e, delta, 0.0f); // start plane
            e = clipToPlane(s, e, delta, 1.0f); // end plane
        }

        // Project our line against the plane defined by our cylinder caps
        // Then calculate the closest point this new projected line has to 0,0,0
        s = s - dot(s,delta) * delta;
        e = e - dot(e,delta) * delta;

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
        // 0=(p.n)+d
        // Equation for ray
        // p=f*t+o
        // Plug ray into plane
        // 0=((f*t+o).n)+d
        // -d=(f*t+o).n
        // -d=f.n*t+o.n
        // -d-o.n=f.n*t
        // -(d+o.n)/(f.n)=t

        float t = -(planeDistance + dot(rayOrigin, planeNormal)) / dot(rayDir, planeNormal);
        return rayDir * t + rayOrigin;
    };
} // namespace IB
