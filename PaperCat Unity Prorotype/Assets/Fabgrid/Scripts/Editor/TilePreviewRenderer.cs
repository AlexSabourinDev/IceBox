using UnityEngine;

namespace Fabgrid
{
    public class TilePreviewRenderer
    {
        private Tilemap3D tilemap;

        public TilePreviewRenderer(Tilemap3D tilemap)
        {
            this.tilemap = tilemap;
        }

        public void Render(Vector3 position)
        {
            if (tilemap.selectedTile?.prefab == null) return;
            if (tilemap.tilePreviewMesh == null) return;

            tilemap.tilePreviewMaterial.SetPass(0);
            Graphics.DrawMeshNow(tilemap.tilePreviewMesh,
                position,
                tilemap.tileRotation);
        }
    }
}