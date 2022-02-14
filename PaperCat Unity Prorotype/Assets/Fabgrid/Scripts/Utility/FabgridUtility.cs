using System.Threading;

#if UNITY_EDITOR

using UnityEditor;

#endif

using UnityEngine;

namespace Fabgrid
{
    public static class FabgridUtility
    {
        private const int ThreadSleepDuration = 10;

#if UNITY_EDITOR

        public static Texture2D GetTilePreviewIcon(GameObject tilePrefab)
        {
            Texture2D tilePreviewTexture = null;

            while (tilePreviewTexture == null)
            {
                if (tilePrefab == null)
                {
                    FabgridLogger.LogError($"The prefab {tilePrefab.name} is null");
                    break;
                }

                tilePreviewTexture = AssetPreview.GetAssetPreview(tilePrefab);
                Thread.Sleep(ThreadSleepDuration);
            }

            return tilePreviewTexture;
        }

#endif

        public static Bounds GetTileWorldBounds(GameObject tile, SizeCalculationOption sizeCalculationOption, Tilemap3D tilemap)
        {
            switch (sizeCalculationOption)
            {
                case SizeCalculationOption.Collider:
                    return GetColliderWorldBounds(tile);

                case SizeCalculationOption.Mesh:
                    return GetMeshWorldBounds(tile);

                case SizeCalculationOption.Custom:
                    return new Bounds(tilemap.newTile.offset, tilemap.newTile.size);

                default:
                    throw new System.Exception($"Fabgrid Error: The SizeCalculationOption {sizeCalculationOption} is unknown.");
            }
        }

        private static Bounds GetColliderWorldBounds(GameObject prefab)
        {
            // AlexS: Since we now correctly instantiate our gameobject, we don't need to worry about all the extra logic being done here before.
            return GetFirstComponent<Collider>(prefab).bounds;
        }

        private static Bounds GetMeshWorldBounds(GameObject prefab)
        {
            var min = Vector3.one * float.MaxValue;
            var max = Vector3.one * float.MinValue;

            foreach(var meshFilter in prefab.GetComponentsInChildren<MeshFilter>())
            {
                if (meshFilter.sharedMesh == null)
                    continue;

                foreach (var vertex in meshFilter.sharedMesh.vertices)
                {
                    min.x = Mathf.Min(min.x, vertex.x);
                    min.y = Mathf.Min(min.y, vertex.y);
                    min.z = Mathf.Min(min.z, vertex.z);

                    max.x = Mathf.Max(max.x, vertex.x);
                    max.y = Mathf.Max(max.y, vertex.y);
                    max.z = Mathf.Max(max.z, vertex.z);
                }
            }

            var b = new Bounds();
            b.SetMinMax(min, max);
            return b;
        }

        public static bool HasAnyComponent<T>(GameObject gameObject) where T : Component
        {
            var components = gameObject.GetComponentsInChildren<T>(true);
            return components.Length > 0;
        }

        public static T GetFirstComponent<T>(GameObject gameObject) where T : Component
        {
            var components = gameObject.GetComponentsInChildren<T>(true);
            if (components.Length > 0)
            {
                return components[0];
            }

            return null;
        }

        public static T GetClosestOfType<T>(T[] components, Vector3 point) where T : Component
        {
            var closestSqrDistance = float.MaxValue;
            T closest = null;
            for (int i = 0; i < components.Length; ++i)
            {
                var sqrDistance = (point - components[i].transform.position).sqrMagnitude;
                if (sqrDistance < closestSqrDistance)
                {
                    closest = components[i];
                    closestSqrDistance = sqrDistance;
                }
            }

            return closest;
        }
    }
}