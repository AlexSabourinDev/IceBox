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

        public static Bounds GetWorldBounds(GameObject gameObject, SizeCalculationOption sizeCalculationOption, Tilemap3D tilemap)
        {
            switch (sizeCalculationOption)
            {
                case SizeCalculationOption.Collider:
                    return GetColliderWorldBounds(gameObject);

                case SizeCalculationOption.Mesh:
                    var renderer = GetFirstComponent<Renderer>(gameObject);
                    return renderer.bounds;

                case SizeCalculationOption.Custom:
                    return tilemap.selectedTile?.prefab == null ?
                        new Bounds(gameObject.transform.position, Vector3.one) :
                        new Bounds(gameObject.transform.position, tilemap.selectedTile.customSize);

                default:
                    throw new System.Exception($"Fabgrid Error: The SizeCalculationOption {sizeCalculationOption} is unknown.");
            }
        }

        private static Bounds GetColliderWorldBounds(GameObject prefab)
        {
            var collider = GetFirstComponent<Collider>(prefab);
            var bounds = collider.bounds;
            bounds.center = prefab.transform.position;

            if (collider is BoxCollider boxCollider)
            {
                bounds.center += boxCollider.center;
            }
            else if (collider is SphereCollider sphereCollider)
            {
                bounds.center += sphereCollider.center;
            }
            else if (collider is CapsuleCollider capsuleCollider)
            {
                bounds.center += capsuleCollider.center;
            }

            return bounds;
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