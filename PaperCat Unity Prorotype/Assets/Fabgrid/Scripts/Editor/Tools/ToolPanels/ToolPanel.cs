#if UNITY_EDITOR

using UnityEditor;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public abstract class ToolPanel
    {
        protected Tilemap3D tilemap;
        protected SerializedObject serializedTilemap;
        protected VisualElement root;

        protected ToolPanel(Tilemap3D tilemap, VisualElement root)
        {
            this.tilemap = tilemap;
            this.serializedTilemap = new SerializedObject(tilemap);
            this.root = root;
            CreatePanel();
        }

        public abstract void CreatePanel();

        public abstract void DestroyPanel();
    }
}

#endif