#if UNITY_EDITOR

using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class RectangleToolPanel : ToolPanel
    {
        private VisualElement toolPanel;
        private VisualElement panel;
        private ObjectField parentField;
        private EnumField offsetOptionField;
        private Vector2Field offsetMultiplierField;

        public RectangleToolPanel(Tilemap3D tilemap, VisualElement root) : base(tilemap, root)
        {
        }

        public override void CreatePanel()
        {
            var fabgridFolder = PathUtility.GetFabgridFolder();
            var visualTree = AssetDatabase.LoadAssetAtPath<VisualTreeAsset>
                ($"{fabgridFolder}/Scripts/Editor/ToolPanels/RectangleToolPanel.uxml");

            panel = visualTree.CloneTree();

            toolPanel = root.Q("tool-panel");
            toolPanel.Add(panel);

            offsetOptionField = toolPanel.Q<EnumField>("offset-option-field");
            offsetOptionField.Bind(serializedTilemap);

            offsetMultiplierField = toolPanel.Q<Vector2Field>("offset-multiplier-field");
            offsetMultiplierField.Bind(serializedTilemap);
        }

        public override void DestroyPanel()
        {
            offsetOptionField.Unbind();
            offsetMultiplierField.Unbind();

            toolPanel.Remove(panel);
        }
    }
}

#endif