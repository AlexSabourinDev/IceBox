#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public class MeshTilePositionCalculator : TilePositionCalculator
    {
        public MeshTilePositionCalculator(Tilemap3D tilemap) : base(tilemap)
        {
        }

        public override Vector3 GetPosition(Vector2 mousePosition)
        {
            var tilePosition = Vector3.negativeInfinity;

            if (tilemap.selectedGameObject != null)
            {
                var worldRay = HandleUtility.GUIPointToWorldRay(mousePosition);
                var hit = FabgridRaycaster.AccurateMeshRaycast(worldRay, tilemap);
                if (hit.transform != null)
                {
                    tilePosition = hit.point;
                    var distanceThroughTile = tilemap.selectedTile.GetDistanceThroughTile(tilePosition, tilemap.tileRotation, hit.normal);
                    tilePosition += hit.normal.normalized * distanceThroughTile * 0.5f;
                    var offset = tilemap.selectedTile.GetOffset(tilePosition, tilemap.tileRotation, tilemap);
                    tilePosition += offset;
                    return tilePosition;
                }
            }
            else
            {
                tilePosition = new FloorTilePositionCalculator(tilemap).GetPosition(mousePosition);
            }

            return tilePosition;
        }
    }
}

#endif