#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public class Brush : Tool
    {
        private TilePreviewRenderer tilePreviewRenderer;
        private TileRotator tileRotator;

        public Brush(Tilemap3D tilemap) : base(tilemap)
        {
            toolType = ToolType.Brush;
            var fabgridPath = PathUtility.GetFabgridFolder();
            tilemap.tilePreviewMaterial = AssetDatabase.LoadAssetAtPath<Material>($"{fabgridPath}/Materials/FabgridPreviewMaterial.mat");
            tilePreviewRenderer = new TilePreviewRenderer(tilemap);
            tileRotator = new TileRotator(tilemap);
        }

        public override void OnKeyDown(Event e)
        {
            tileRotator.OnKeyDown(e);
        }

        public override void OnMouseDown(Event e)
        {
            if (!EvaluatePreConditions(e)) return;

            var position = positionCalculator.GetPosition(e.mousePosition);

            CreateTileAt(position);

            e.Use();
        }

        private bool EvaluatePreConditions(Event e)
        {
            if (e.button != 0) return false;
            if (e.alt || Input.GetKey(KeyCode.LeftAlt)) return false;
            if (e.control || Input.GetKey(KeyCode.LeftControl)) return false;
            if (tilemap.selectedTile == null || tilemap.selectedTile.prefab == null) return false;

            return true;
        }

        public override void OnRender(Event e)
        {
            if (tilemap.selectedTile?.prefab == null) return;

            tilePreviewRenderer.Render(positionCalculator.GetPosition(e.mousePosition));
        }
    }
}

#endif