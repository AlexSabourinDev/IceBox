using UnityEditor;
using UnityEngine;
using UnityEngine.InputSystem;

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
            if (e.button != 0) return;
            if (e.alt || Keyboard.current.leftAltKey.isPressed) return;
            if (e.control || Keyboard.current.leftCtrlKey.isPressed) return;
            if (tilemap.selectedTile == null || tilemap.selectedTile.prefab == null) return;

            var position = positionCalculator.GetPosition(e.mousePosition);

            CreateTileAt(position);

            e.Use();
        }

        public override void OnRender(Event e)
        {
            if (tilemap.selectedTile?.prefab == null) return;

            tilePreviewRenderer.Render(positionCalculator.GetPosition(e.mousePosition));
        }
    }
}