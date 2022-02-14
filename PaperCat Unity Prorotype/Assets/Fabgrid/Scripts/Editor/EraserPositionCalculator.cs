#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public static class EraserPositionCalculator
    {
        public static Vector3 GetPosition(Vector2 mousePosition, Tilemap3D tilemap)
        {
            var ray = HandleUtility.GUIPointToWorldRay(mousePosition);
            var hit = FabgridRaycaster.InaccurateMeshRaycast(ray, tilemap);
            if (hit.transform != null)
            {
                return hit.point;
            }

            return Vector3.negativeInfinity;
        }
    }
}

#endif