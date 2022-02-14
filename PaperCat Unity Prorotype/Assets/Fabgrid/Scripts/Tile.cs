using UnityEngine;

namespace Fabgrid
{
    [System.Serializable]
    public class Tile
    {
        public GameObject prefab;
        public Vector3 offset;
        public Vector3 size;
        public SizeCalculationOption sizeCalculationOption;
        [HideInInspector]
        public Texture2D thumbnail;
        public Category category = null;

        public Bounds GetWorldBounds(Vector3 position, Quaternion rotation, Tilemap3D tilemap)
        {
            if (position.IsInfinity() || prefab == null)
                return new Bounds(Vector3.zero, Vector3.one);

            return new Bounds(offset + position, rotation * size);
        }

        public Vector3 GetCenterToSurfaceVector(Vector3 position, Quaternion rotation, Vector3 direction, Tilemap3D tilemap)
        {
            if (position.IsInfinity()) return Vector3.negativeInfinity;

            var worldBounds = GetWorldBounds(position, rotation, tilemap);
            var closestPoint = worldBounds.ClosestPoint(worldBounds.center + (direction.normalized * float.MaxValue));
            return worldBounds.center - closestPoint;
        }

        public Vector3 GetOffset(Vector3 position, Quaternion rotation, Tilemap3D tilemap)
        {
            var center = GetWorldBounds(position, rotation, tilemap).center;
            return position - center;
        }

        public float GetDistanceThroughTile(Vector3 position, Quaternion rotation, Vector3 direction)
        {
            if (position.IsInfinity()) return Mathf.NegativeInfinity;

            var copy = Object.Instantiate(prefab);
            copy.transform.position = position;
            copy.transform.rotation = rotation;

            var renderers = copy.GetComponentsInChildren<Renderer>();
            if (renderers.Length == 0) return 0f;

            var meshTransform = renderers[0].transform;

            var meshCollider = meshTransform.gameObject.AddComponent<MeshCollider>();
            meshCollider.convex = true;

            var size = meshCollider.bounds.size.magnitude;

            var upperPoint = meshCollider.ClosestPoint(meshCollider.transform.position + (direction * size));
            var lowerPoint = meshCollider.ClosestPoint(meshCollider.transform.position - (direction * size));
            var distance = Vector3.Distance(upperPoint, lowerPoint);

            Object.DestroyImmediate(copy);

            return distance;
        }
    }
}