using UnityEngine;

namespace Fabgrid
{
    public static class FabgridMath
    {
        public static Vector3 Frac(Vector3 v)
        {
            return Repeat(v, 1f);
        }

        public static Vector3 Repeat(Vector3 v, float length)
        {
            return new Vector3(Mathf.Repeat(v.x, length), Mathf.Repeat(v.y, length), Mathf.Repeat(v.z, length));
        }
    }
}