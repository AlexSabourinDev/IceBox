using UnityEngine;

namespace Fabgrid
{
    public struct FabgridRaycastHit
    {
        public Vector3 point;
        public Vector3 normal;
        public Transform transform;

        public FabgridRaycastHit(Vector3 point, Vector3 normal, Transform transform)
        {
            this.point = point;
            this.normal = normal;
            this.transform = transform;
        }
    }
}