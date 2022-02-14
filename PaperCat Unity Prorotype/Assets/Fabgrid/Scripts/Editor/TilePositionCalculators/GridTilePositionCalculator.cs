#if UNITY_EDITOR

using UnityEngine;

namespace Fabgrid
{
    public class GridTilePositionCalculator : TilePositionCalculator
    {
        public GridTilePositionCalculator(Tilemap3D tilemap) : base(tilemap)
        {
        }

        public override Vector3 GetPosition(Vector2 mousePosition)
        {
            var position = tilemap.MouseToGridPosition(mousePosition);
            if (tilemap.selectedTile?.prefab != null)
            {
                var intersectionOffset = tilemap.selectedTile.GetCenterToSurfaceVector(position, tilemap.tileRotation, Vector3.down, tilemap);
                position += intersectionOffset;
                position += tilemap.selectedTile.GetOffset(position, tilemap.tileRotation, tilemap);
                return position;
            }

            return position;
        }
    }
}

#endif