#if UNITY_EDITOR

using UnityEngine.UIElements;

namespace Fabgrid
{
    public class ToolPanelFactory
    {
        private readonly Tilemap3D tilemap;
        private readonly VisualElement root;

        public ToolPanelFactory(Tilemap3D tilemap, VisualElement root)
        {
            this.tilemap = tilemap;
            this.root = root;
        }

        public ToolPanel Create(ToolType toolType)
        {
            switch (toolType)
            {
                case ToolType.Brush:
                    return new BrushToolPanel(tilemap, root);

                case ToolType.Eraser:
                    return new EraserToolPanel(tilemap, root);

                case ToolType.RectangleTool:
                    return new RectangleToolPanel(tilemap, root);

                case ToolType.SelectionBox:
                    return null;

                default:
                    throw new System.Exception($"Fabgrid Error: The tool type {toolType} is not recognized.");
            }
        }
    }
}

#endif