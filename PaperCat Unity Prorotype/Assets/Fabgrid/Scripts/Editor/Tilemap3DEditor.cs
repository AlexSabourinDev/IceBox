#if UNITY_EDITOR
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    [CustomEditor(typeof(Tilemap3D))]
    [ExecuteInEditMode]
    public class Tilemap3DEditor : Editor
    {
        private Tilemap3D tilemap;
        private VisualElement root;
        private VisualTreeAsset visualTree;
        private VisualElement selectedPanel;
        private Dictionary<string, VisualTreeAsset> panelAssets = new Dictionary<string, VisualTreeAsset>();
        private FSM fsm;
        private List<Panel> panels = new List<Panel>();

        private StyleSheet darkTheme;
        private StyleSheet lightTheme;

        private string fabgridFolder;

        public State CurrentState => fsm.CurrentState;

        public VisualElement Root => root;

        private void OnEnable()
        {
            fabgridFolder = PathUtility.GetFabgridFolder();

            tilemap = target as Tilemap3D;
            tilemap.tileRotation = Quaternion.identity;

            root = new VisualElement();

            visualTree = AssetDatabase.LoadAssetAtPath<VisualTreeAsset>($"{fabgridFolder}/Scripts/Editor/Tilemap3DEditor.uxml");

            panels.Clear();
            panels.Add(
                new Panel("paint-panel",
                $"{PathUtility.PanelsPath}/PaintPanel.uss",
                $"{PathUtility.PanelsPath}/PaintPanel.uxml",
                new Paint(root, tilemap), $"{fabgridFolder}/Textures/PaintButtonIcon.png"));

            panels.Add(
                new Panel("add-tile-panel",
                $"{PathUtility.PanelsPath}/AddTilePanel.uss",
                $"{PathUtility.PanelsPath}/AddTilePanel.uxml",
                new AddTile(root, tilemap),
                $"{fabgridFolder}/Textures/AddTileButtonIcon.png"));

            panels.Add(
                new Panel("configuration-panel",
                $"{PathUtility.PanelsPath}/ConfigurationPanel.uss",
                $"{PathUtility.PanelsPath}/ConfigurationPanel.uxml",
                new Configurate(root, tilemap),
                $"{fabgridFolder}/Textures/ConfigurateButtonIcon.png"));

            //panels.Add(
            //    new Panel("batching-panel",
            //    $"{PathUtility.PanelsPath}/BatchingPanel.uss",
            //    $"{PathUtility.PanelsPath}/BatchingPanel.uxml",
            //    new Batching(root, tilemap),
            //    $"{fabgridFolder}/Textures/BatchingButtonIcon.png"));

            panels.Add(
                new Panel("help-panel",
                $"{PathUtility.PanelsPath}/HelpPanel.uss",
                $"{PathUtility.PanelsPath}/HelpPanel.uxml",
                new Help(root),
                $"{fabgridFolder}/Textures/HelpButtonIcon.png"));

            fsm = new FSM();
            SetupPanelAssets();

            AddStyleSheets();

            LoadThemes();
            ApplyCurrentTheme();

            SceneView.duringSceneGui += OnDuringSceneGUI;
        }

        private void AddStyleSheets()
        {
            root.styleSheets.Add(AssetDatabase.LoadAssetAtPath<StyleSheet>($"{fabgridFolder}/Scripts/Editor/Tilemap3DEditor.uss"));
            
            foreach (var panel in panels)
                root.styleSheets.Add(AssetDatabase.LoadAssetAtPath<StyleSheet>(panel.StylesheetPath));
        }

        private void LoadThemes()
        {
            darkTheme = AssetDatabase.LoadAssetAtPath<StyleSheet>($"{fabgridFolder}/Scripts/Editor/Themes/Dark.uss");
            lightTheme = AssetDatabase.LoadAssetAtPath<StyleSheet>($"{fabgridFolder}/Scripts/Editor/Themes/Light.uss");
        }

        private void ApplyCurrentTheme()
        {
            RemoveAppliedThemes();

            if (EditorGUIUtility.isProSkin)
            {
                root.styleSheets.Add(darkTheme);
            }
            else 
            {
                root.styleSheets.Add(lightTheme);
            }
        }

        private void RemoveAppliedThemes()
        {
            if (root.styleSheets.Contains(darkTheme))
            {
                root.styleSheets.Remove(darkTheme);
            }

            if (root.styleSheets.Contains(lightTheme))
            {
                root.styleSheets.Remove(lightTheme);
            }
        }

        private void OnDisable()
        {
            SceneView.duringSceneGui -= OnDuringSceneGUI;
            fsm.OnDestroy();
        }

        private void OnDuringSceneGUI(SceneView sceneView)
        {
            if (tilemap == null) return;

            HandleUtility.AddDefaultControl(GUIUtility.GetControlID(FocusType.Passive));
            fsm.OnSceneGUI();
        }

        private void SetupPanelAssets()
        {
            panelAssets.Clear();

            foreach (var panel in panels)
                panelAssets.Add(panel.Name, AssetDatabase.LoadAssetAtPath<VisualTreeAsset>(panel.VisualTreeAssetPath));
        }

        public override VisualElement CreateInspectorGUI()
        {
            root.Clear();
            root.Add(visualTree.CloneTree());
            SetupControls();
            SelectPanel("paint-panel", root.Q<Button>("paint-panel-button"), new Paint(root, tilemap));
            return root;
        }

        public override void OnInspectorGUI()
        {
            base.OnInspectorGUI();
            fsm.OnInspectorGUI();
        }

        private void SetupControls()
        {
            foreach(var panel in panels)
            {
                var button = root.Q<Button>($"{panel.Name}-button");
                button.clickable.clicked += () => OnClickNavigationBarButton(panel.Name, button, panel.State);
                AddNavigationButtonIcon(button, panel.ButtonIconPath);
            }
        }

        private void SelectPanel(string panelName, Button navigationBarButton, State state)
        {
            root.Query<Button>(null, "navigation-bar-button").ForEach(button =>
            {
                if (button.ClassListContains("selected-navigation-bar-button"))
                {
                    button.RemoveFromClassList("selected-navigation-bar-button");
                }
            });

            var panelContainer = root.Q<VisualElement>("main-panel-viewport");

            if (selectedPanel != null)
            {
                selectedPanel.RemoveFromHierarchy();
            }

            var instantiatedTemplate = panelAssets[panelName].CloneTree();
            panelContainer.Add(instantiatedTemplate);
            selectedPanel = instantiatedTemplate;

            navigationBarButton.AddToClassList("selected-navigation-bar-button");
            fsm.Transition(state);
        }

        private void AddNavigationButtonIcon(Button button, string iconPath)
        {
            var icon = new Image
            {
                image = AssetDatabase.LoadAssetAtPath<Texture2D>(iconPath)
            };
            icon.AddToClassList("navigation-bar-button-icon");
            button.Add(icon);
        }

        private void OnClickNavigationBarButton(string panelName, Button button, State state)
        {
            SelectPanel(panelName, button, state);
        }

        private void OnDestroy()
        {
            fsm.OnDestroy();
        }
    }
}
#endif