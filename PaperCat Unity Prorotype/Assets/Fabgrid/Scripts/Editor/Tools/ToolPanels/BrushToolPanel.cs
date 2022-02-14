#if UNITY_EDITOR

using UnityEngine.UIElements;

namespace Fabgrid
{
    public class BrushToolPanel : ToolPanel
    {
        public BrushToolPanel(Tilemap3D tilemap, VisualElement root) : base(tilemap, root)
        {
        }

        public override void CreatePanel()
        {
        }

        public override void DestroyPanel()
        {
        }
    }
}

#endif