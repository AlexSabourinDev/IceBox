#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class FabgridHelpWindow : EditorWindow
    {
        private static readonly Vector2 Size = new Vector2(450f, 600f);
        private const string WindowTitle = "Fabgrid Help";

        [MenuItem("Help/Fabgrid/Getting Started")]
        private static void ShowHelpWindow()
        {
            FabgridHelpWindow window = EditorWindow.GetWindow<FabgridHelpWindow>();
            window.titleContent = new GUIContent(WindowTitle);
            var offset = Size * 0.5f;
            var position = new Vector2((Screen.currentResolution.width * 0.5f) - offset.x, (Screen.currentResolution.height * 0.5f) - offset.y);
            window.position = new Rect(position, Size);
            window.Show();
        }

        private void OnEnable()
        {
            var fabgridFolder = PathUtility.GetFabgridFolder();
            var visualTree = AssetDatabase.LoadAssetAtPath<VisualTreeAsset>($"{fabgridFolder}/Scripts/Editor/FabgridHelpWindow/FabgridHelpWindow.uxml");

            var stylesheet = AssetDatabase.LoadAssetAtPath<StyleSheet>($"{fabgridFolder}/Scripts/Editor/Tilemap3DEditor.uss");
            rootVisualElement.styleSheets.Add(stylesheet);

            rootVisualElement.Add(visualTree.CloneTree());
        }
    }
}

#endif