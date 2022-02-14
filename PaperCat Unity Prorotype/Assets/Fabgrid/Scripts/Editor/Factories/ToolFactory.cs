#if UNITY_EDITOR

namespace Fabgrid
{
    public class ToolFactory
    {
        private readonly Tilemap3D tilemap;

        public ToolFactory(Tilemap3D tilemap)
        {
            this.tilemap = tilemap;
        }

        public Tool Create(ToolType toolType)
        {
            switch (toolType)
            {
                case ToolType.Brush:
                    return new Brush(tilemap);

                case ToolType.Eraser:
                    return new Eraser(tilemap);

                case ToolType.RectangleTool:
                    return new RectangleTool(tilemap);

                case ToolType.SelectionBox:
                    return new SelectionBox(tilemap);

                default:
                    throw new System.Exception($"Fabgrid Error: The tool type {toolType} is not recognized.");
            }
        }
    }
}

#endif