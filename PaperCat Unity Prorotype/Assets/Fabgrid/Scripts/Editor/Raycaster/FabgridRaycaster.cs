using System.Collections.Generic;
using UnityEngine;

namespace Fabgrid
{
    internal struct SortedRenderer
    {
        public Renderer renderer;
        public float distance;
    }

    public static class FabgridRaycaster
    {
        public static FabgridRaycastHit AccurateMeshRaycast(Ray ray, Tilemap3D tilemap)
        {
            var renderers = tilemap.GetComponentsInChildren<Renderer>();
            var intersectingRenderer = GetIntersectingRenderer(renderers, ray);

            SortedRenderer[] sortedRenderers = new SortedRenderer[renderers.Length];
            for (int i = 0; i < renderers.Length; ++i)
            {
                float intersectionDistance;
                if (renderers[i].bounds.IntersectRay(ray, out intersectionDistance))
                {
                    sortedRenderers[i] = new SortedRenderer() { renderer = renderers[i], distance = intersectionDistance };
                }
                else
                {
                    sortedRenderers[i] = new SortedRenderer() { renderer = renderers[i], distance = float.PositiveInfinity };
                }
            }

            System.Array.Sort(sortedRenderers, (SortedRenderer left, SortedRenderer right) => { return left.distance.CompareTo(right.distance); });

            foreach (SortedRenderer renderer in sortedRenderers)
            {
                // Once we reach our infinites, give up
                if (!float.IsFinite(renderer.distance))
                {
                    break;
                }

                var hit = GetPointOnRenderer(renderer.renderer, ray);
                if (float.IsFinite(hit.point.x))
                {
                    return new FabgridRaycastHit(hit.point, hit.normal, intersectingRenderer.transform);
                }
            }

            return new FabgridRaycastHit() { point = Vector3.negativeInfinity };
        }

        public static FabgridRaycastHit InaccurateMeshRaycast(Ray ray, Tilemap3D tilemap)
        {
            var renderers = tilemap.GetComponentsInChildren<Renderer>();
            var intersectingRenderer = GetIntersectingRenderer(renderers, ray);

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

        private static Renderer GetIntersectingRenderer(Renderer[] renderers, Ray ray)
        {
            // Get all intersecting renderers
            float closestIntersection = float.PositiveInfinity;
            Renderer closestRenderer = null;
            for (int i = 0; i < renderers.Length; ++i)
            {
                float intersectionDistance;
                if (renderers[i].bounds.IntersectRay(ray, out intersectionDistance))
                {
                    if (intersectionDistance < closestIntersection)
                    {
                        closestRenderer = renderers[i];
                        closestIntersection = intersectionDistance;
                    }
                }
            }

            return closestRenderer;
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
    }
}