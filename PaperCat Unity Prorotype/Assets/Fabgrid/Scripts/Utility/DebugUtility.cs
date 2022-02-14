using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Fabgrid
{
    public static class DebugUtility
    {
        public static void DrawWireSphere(Vector3 position, float radius, Color color)
        {
            int segments = 8;
            float step = 2.0f * Mathf.PI / segments;
            for (int i = 0; i < segments; ++i)
            {
                var direction = new Vector3(Mathf.Cos(step * i), 0.0f, Mathf.Sin(step * i));
                DrawDisc(position, radius, direction, Vector3.up, color);
            }
        }

        public static void DrawDisc(Vector3 position, float radius, Vector3 up, Vector3 right, Color color)
        {
            up = up.normalized;
            right = right.normalized;
            Vector3 forward = Vector3.Cross(right, up);

            int segments = 8;
            float step = 2.0f * Mathf.PI / segments;
            for (int i = 0; i < segments; ++i)
            {
                Vector3 start = position + right * Mathf.Cos(step * i) * radius + forward * Mathf.Sin(step * i) * radius;
                Vector3 end = position + right * Mathf.Cos(step * (i + 1)) * radius + forward * Mathf.Sin(step * (i + 1)) * radius;
                Debug.DrawLine(start, end, color);
            }
        }

        public static void DrawWireBoundingBox(Bounds bounds, Color color)
        {
            DrawWireBoundingBox(bounds.min, bounds.max, color);
        }

        public static void DrawWireBoundingBox(Vector3 min, Vector3 max, Color color)
        {
            var a = new Vector3(max.x, min.y, min.z);
            var b = new Vector3(max.x, min.y, max.z);
            var c = new Vector3(min.x, min.y, max.z);
            var d = new Vector3(min.x, max.y, min.z);
            var e = new Vector3(max.x, max.y, min.z);
            var f = new Vector3(min.x, max.y, max.z);

            Debug.DrawLine(min, a, color);
            Debug.DrawLine(a, b, color);
            Debug.DrawLine(b, c, color);
            Debug.DrawLine(c, min, color);
            Debug.DrawLine(min, d, color);
            Debug.DrawLine(a, e, color);
            Debug.DrawLine(b, max, color);
            Debug.DrawLine(c, f, color);
            Debug.DrawLine(d, e, color);
            Debug.DrawLine(e, max, color);
            Debug.DrawLine(max, f, color);
            Debug.DrawLine(f, d, color);
        }
    }
}
