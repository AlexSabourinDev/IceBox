using UnityEngine;

namespace Fabgrid
{
    public abstract class TilePositionCalculator
    {
        protected Tilemap3D tilemap;

        protected TilePositionCalculator(Tilemap3D tilemap)
        {
            this.tilemap = tilemap;
        }

        public abstract Vector3 GetPosition(Vector2 mousePosition);
    }
}