#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public class AdaptiveTilePositionCalculator : TilePositionCalculator
    {
        public AdaptiveTilePositionCalculator(Tilemap3D tilemap) : base(tilemap)
        {
        }

        public override Vector3 GetPosition(Vector2 mousePosition)
        {
            Vector3 tilePosition;
            var floorPosition = tilemap.GetFloorPosition();

            if (tilemap.selectedGameObject != null && tilemap.selectedGameObject.transform.position.y >= floorPosition.y)
            {
                var ray = HandleUtility.GUIPointToWorldRay(mousePosition);
                var hit = FabgridRaycaster.InaccurateMeshRaycast(ray, tilemap);
                if (hit.transform != null)
                {
                    var gridPosition = tilemap.WorldToGridPosition(hit.point);
                    tilePosition = new Vector3(gridPosition.x,
                        hit.point.y,
                        gridPosition.z);

                    tilePosition += tilemap.selectedTile.GetOffset(tilePosition, tilemap.tileRotation, tilemap);
                    tilePosition += Vector3.up * tilemap.selectedTile.GetWorldBounds(tilePosition, tilemap.tileRotation, tilemap).extents.y;

                    return tilePosition;
                }
            }

            tilePosition = tilemap.MouseToGridPosition(mousePosition);
            if (tilemap.selectedTile != null)
            {
                tilePosition += tilemap.selectedTile.GetCenterToSurfaceVector(tilePosition, tilemap.tileRotation, Vector3.down, tilemap);
                tilePosition += tilemap.selectedTile.GetOffset(tilePosition, tilemap.tileRotation, tilemap);
            }

            return tilePosition;
        }
    }
}

#endif