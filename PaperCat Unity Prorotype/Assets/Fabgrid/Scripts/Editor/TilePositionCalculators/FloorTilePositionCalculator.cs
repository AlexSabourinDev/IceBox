#if UNITY_EDITOR

using UnityEngine;

namespace Fabgrid
{
    public class FloorTilePositionCalculator : TilePositionCalculator
    {
        public FloorTilePositionCalculator(Tilemap3D tilemap) : base(tilemap)
        {
        }

        public override Vector3 GetPosition(Vector2 mousePosition)
        {
            var position = tilemap.MouseToGridPlanePosition(mousePosition);
            if (tilemap.selectedTile != null)
            {
                var intersectionOffset = tilemap.selectedTile.GetCenterToSurfaceVector(position, tilemap.tileRotation, Vector3.down, tilemap);
                position += intersectionOffset;
                position += tilemap.selectedTile.GetOffset(position, tilemap.tileRotation, tilemap);
                return position;
            }

            return Vector3.negativeInfinity;
        }
    }
}

#endif