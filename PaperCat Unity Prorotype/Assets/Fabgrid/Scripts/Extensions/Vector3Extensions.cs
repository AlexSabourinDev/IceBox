using UnityEngine;

namespace Fabgrid
{
    public static class Vector3Extensions
    {
        public static bool Approximately(this Vector3 v1, Vector3 v2)
        {
            return Mathf.Approximately(v1.x, v2.x) &&
                   Mathf.Approximately(v1.y, v2.y) &&
                   Mathf.Approximately(v1.z, v2.z);
        }

        public static bool IsInfinity(this Vector3 v)
        {
            if (v.ToString() == Vector3.positiveInfinity.ToString()) return true;
            if (v.ToString() == Vector3.negativeInfinity.ToString()) return true;
            if (v.ToString() == new Vector3(float.NaN, float.NaN, float.NaN).ToString()) return true;

            return false;
        }
    }
}