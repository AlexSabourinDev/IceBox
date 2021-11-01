using UnityEngine;

namespace Fabgrid
{
    public class TileRotator
    {
        private Tilemap3D tilemap;

        public TileRotator(Tilemap3D tilemap)
        {
            this.tilemap = tilemap;
        }

        public void OnKeyDown(Event e)
        {
            if (e.control) return;

            if (e.keyCode == tilemap.resetTileRotationKey)
            {
                ResetSelectedTileRotation();
                e.Use();
            }
            else if (e.keyCode == tilemap.rotateTileAroundYKey)
            {
                if (e.shift)
                {
                    RotateSelectedTile(Vector3.down);
                }
                else
                {
                    RotateSelectedTile(Vector3.up);
                }

                e.Use();
            }
            else if (e.keyCode == tilemap.rotateTileAroundZKey)
            {
                if (e.shift)
                {
                    RotateSelectedTile(Vector3.back);
                }
                else
                {
                    RotateSelectedTile(Vector3.forward);
                }

                e.Use();
            }
        }

        private void ResetSelectedTileRotation()
        {
            tilemap.tileRotation = Quaternion.identity;
        }

        private void RotateSelectedTile(Vector3 axis)
        {
            tilemap.tileRotation = Quaternion.AngleAxis(tilemap.rotationStepAngle, axis) * tilemap.tileRotation;
            tilemap.tileRotation.Normalize();
        }
    }
}