using System.Collections.Generic;
using UnityEngine;

namespace Fabgrid
{
    public static class FabgridRaycaster
    {
        public static FabgridRaycastHit AccurateMeshRaycast(Ray ray, Tilemap3D tilemap)
        {
            var intersectingRenderer = GetIntersectingRenderer(GetRenderers(tilemap), ray);

            if (intersectingRenderer != null)
            {
                var hit = GetPointOnRenderer(intersectingRenderer, ray);
                return new FabgridRaycastHit(hit.point, hit.normal, intersectingRenderer.transform);
            }

            return new FabgridRaycastHit() { point = Vector3.negativeInfinity };
        }

        public static FabgridRaycastHit InaccurateMeshRaycast(Ray ray, Tilemap3D tilemap)
        {
            var intersectingRenderer = GetIntersectingRenderer(GetRenderers(tilemap), ray);

            if (intersectingRenderer != null)
            {
                if (intersectingRenderer.bounds.IntersectRay(ray, out float distance))
                {
                    return new FabgridRaycastHit(default, default, default)
                    {
                        point = ray.GetPoint(distance),
                        transform = intersectingRenderer.transform
                    };
                }
            }

            return new FabgridRaycastHit() { point = Vector3.negativeInfinity };
        }

        public static FabgridRaycastHit ColliderCast(Ray ray, Tilemap3D tilemap)
        {
            var colliders = tilemap.GetComponentsInChildren<Collider>();
            var intersectingCollider = GetIntersectingCollider(colliders, ray);

            if (intersectingCollider != null)
            {
                if (intersectingCollider.bounds.IntersectRay(ray, out float distance))
                {
                    return new FabgridRaycastHit(default, default, default)
                    {
                        point = ray.GetPoint(distance),
                        transform = intersectingCollider.transform
                    };
                }
            }

            return new FabgridRaycastHit() { point = Vector3.negativeInfinity };
        }

        private static Renderer GetIntersectingRenderer(Renderer[] renderers, Ray ray)
        {
            var intersectingRenderers = new List<Renderer>();

            // Get all intersecting renderers
            for (int i = 0; i < renderers.Length; ++i)
            {
                if (renderers[i].bounds.IntersectRay(ray))
                {
                    intersectingRenderers.Add(renderers[i]);
                }
            }

            if (intersectingRenderers.Count == 0) return null;

            return FabgridUtility.GetClosestOfType<Renderer>(
                    intersectingRenderers.ToArray(),
                    ray.origin);
        }

        public static Collider GetIntersectingCollider(Collider[] colliders, Ray ray)
        {
            var intersectingColliders = new List<Collider>();

            // Get all intersecting renderers
            for (int i = 0; i < colliders.Length; ++i)
            {
                if (colliders[i].bounds.IntersectRay(ray))
                {
                    intersectingColliders.Add(colliders[i]);
                }
            }

            if (intersectingColliders.Count == 0) return null;

            return FabgridUtility.GetClosestOfType<Collider>(
                    intersectingColliders.ToArray(),
                    ray.origin);
        }

        private static RaycastHit GetPointOnRenderer(Renderer renderer, Ray ray)
        {
            var meshCollider = renderer.gameObject.AddComponent<MeshCollider>();

            if (meshCollider.Raycast(ray, out RaycastHit hit, float.MaxValue))
            {
                Object.DestroyImmediate(meshCollider);
                return hit;
            }

            Object.DestroyImmediate(meshCollider);
            return new RaycastHit() { point = Vector3.negativeInfinity };
        }

        private static Renderer[] GetRenderers(Tilemap3D tilemap)
        {
            var renderers = new List<Renderer>();
            renderers.AddRange(tilemap.GetComponentsInChildren<Renderer>());

            foreach (var layer in tilemap.layers)
            {
                if (layer == null) continue;
                renderers.AddRange(layer.GetComponentsInChildren<Renderer>());
            }

            return renderers.ToArray();
        }
    }
}