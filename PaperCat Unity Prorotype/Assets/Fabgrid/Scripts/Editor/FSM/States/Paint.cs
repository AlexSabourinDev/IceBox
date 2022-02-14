#if UNITY_EDITOR

using System.Collections.Generic;
using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.Assertions;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Paint : State
    {
        private readonly Tilemap3D tilemap;
        private readonly SerializedObject serializedTilemap;
        private Button selectedTileButton;
        private Button filterButton;
        private readonly Dictionary<Button, Tile> selectedButtonTilePairs;
        private readonly List<Tile> selectedTiles = new List<Tile>();
        private readonly List<Button> selectedTileButtons = new List<Button>();
        private readonly Dictionary<Button, Tile> buttonTilePairs;
        private Tool currentTool;
        private ToolPanel currentToolPanel;
        private LayerPanel layerPanel;
        private readonly EventHandler eventHandler;
        private readonly ToolFactory toolFactory;
        private readonly ToolPanelFactory toolPanelFactory;
        private EnumField toolTypeField;
        private TextField searchField;
        private bool shouldFocusOnSceneView = false;
        private bool isHoveringOverRemoveButton = false;

        public LayerPanel LayerPanel => layerPanel;

        public Paint(VisualElement root, Tilemap3D tilemap) : base(root)
        {
            this.tilemap = tilemap;
            this.serializedTilemap = new SerializedObject(tilemap);
            selectedButtonTilePairs = new Dictionary<Button, Tile>();
            buttonTilePairs = new Dictionary<Button, Tile>();
            currentTool = new RectangleTool(tilemap);
            eventHandler = new EventHandler();
            toolFactory = new ToolFactory(tilemap);
            toolPanelFactory = new ToolPanelFactory(tilemap, root);
        }

        public override void OnEnter()
        {
            InitializeFilterButton();

            tilemap.selectedTile = null;

            PopulateTileButtons();

            var mainPanelHeader = root.Q<Label>("main-panel-header");
            mainPanelHeader.text = "Paint";

            SetupControls();

            Undo.undoRedoPerformed += OnUndoRedo;
            RegisterEvents();
            SceneView.RepaintAll();

            root.RegisterCallback<MouseEnterEvent>(_ => shouldFocusOnSceneView = false);
            root.RegisterCallback<MouseLeaveEvent>(_ => shouldFocusOnSceneView = true);

            FilterTileButtons(tilemap.searchValue, tilemap.selectedCategory);

            layerPanel = new LayerPanel(root, tilemap);

            tilemap.OnRefresh += OnRefresh;
        }

        private void InitializeFilterButton()
        {
            filterButton = root.Q<Button>("filter-button");

            var fabgridFolder = PathUtility.GetFabgridFolder();

            var icon = new Image
            {
                image = AssetDatabase.LoadAssetAtPath<Texture2D>($"{fabgridFolder}/Textures/FilterButtonIcon.png")
            };

            icon.AddToClassList("filter-button-icon");
            filterButton.Add(icon);
        }

        private void SetupControls()
        {
            var floorIndexField = root.Q<IntegerField>("floor-index-field");
            floorIndexField.Bind(serializedTilemap);
            floorIndexField.Q<Label>().RemoveFromClassList("unity-base-field__label");
            floorIndexField.Q<Label>().AddToClassList("floor-label");

            toolTypeField = root.Q<EnumField>("tool-type-field");
            toolTypeField.Bind(serializedTilemap);

            SetupSearchField();

            SwitchTool((ToolType)toolTypeField.value);

            RegisterCallbacks();
        }

        private void SetupSearchField()
        {
            searchField = root.Q<TextField>("search-field");
            searchField.Bind(serializedTilemap);
            searchField.RegisterValueChangedCallback(e => FilterTileButtons(e.newValue, tilemap.selectedCategory));
            searchField.RegisterCallback<PointerDownEvent>(e => 
            {
                DeselectTiles();
                e.StopPropagation();
            });
        }

        private void ShowAllTileButtons()
        {
            foreach (var buttonParent in root.Query(null, "tile-button-parent").ToList())
            {
                buttonParent.Show();
            }
        }

        private void FilterTileButtons(string name, Category category)
        {
            if (string.IsNullOrEmpty(name) && category.IsEmpty())
            {
                ShowAllTileButtons();
                return;
            }

            foreach (var buttonParent in root.Query(null, "tile-button-parent").ToList())
            {
                var label = buttonParent.Q<Label>();
                var button = buttonParent.Q<Button>();
                var tile = buttonTilePairs[button];
                Assert.IsNotNull(tile);

                bool shallShow = label.text.IndexOf(name, System.StringComparison.OrdinalIgnoreCase) >= 0;

                if(tilemap.selectedCategory != null && !tilemap.selectedCategory.IsEmpty())
                {
                    shallShow &= tile.category.name == tilemap.selectedCategory.name;
                }

                if (shallShow)
                {
                    buttonParent.Show();
                }
                else
                {
                    buttonParent.Hide();
                }
            }
        }

        private void RegisterCallbacks()
        {
            toolTypeField.RegisterCallback<ChangeEvent<System.Enum>>(e =>
            {
                if (e.newValue == null) return;
                SwitchTool((ToolType)e.newValue);
            });

            root.Q<VisualElement>("main-container").RegisterCallback<PointerDownEvent>(OnClickMainContainer);

            filterButton.clickable.clickedWithEventInfo += OnClickFilterButton;
        }

        private void OnClickFilterButton(EventBase e)
        {
            var targetElement = e.target as VisualElement;

            var menu = new GenericMenu();
            AddCategoryItems(menu);
            menu.DropDown(targetElement.worldBound);
        }

        private void AddCategoryItems(GenericMenu menu)
        {
            foreach (var category in tilemap.categories)
            {
                bool isSelected = category == tilemap.selectedCategory;
                menu.AddItem(new GUIContent(category.name), isSelected, OnChangeCategory, category);
            }

            menu.AddItem(new GUIContent("Edit categories/New category..."), false, OnClickAddCategory);
            menu.AddItem(new GUIContent("Edit categories/Remove category..."), false, OnClickRemoveCategory);
        }

        private void OnChangeCategory(object menuItem)
        {
            var category = menuItem as Category;
            Assert.IsNotNull(category);

            if (tilemap.selectedCategory.name == category.name)
            {
                tilemap.selectedCategory = null;
            }
            else
            {
                tilemap.selectedCategory = category;
            }

            PopulateTileButtons();
            FilterTileButtons(tilemap.searchValue, category);
        }

        private void OnClickAddCategory()
        {
            var popup = EditorWindow.CreateWindow<AddCategoryPopup>("Add category");
            popup.titleContent = new GUIContent("Add category");
            popup.Category = new Category();
            popup.Tilemap = tilemap;

            var position = popup.position;

            position.center = new Vector2(
                Screen.currentResolution.width / 2.0f,
                Screen.currentResolution.height / 2.0f);

            position.width = 400;
            position.height = 100;

            popup.position = position;

            popup.ShowPopup();
        }

        private void OnClickRemoveCategory()
        {
            var popup = EditorWindow.CreateWindow<RemoveCategoryPopup>("Remove category");
            popup.titleContent = new GUIContent("Remove category");
            popup.Tilemap = tilemap;

            var position = popup.position;

            position.center = new Vector2(
                Screen.currentResolution.width / 2.0f,
                Screen.currentResolution.height / 2.0f);

            position.width = 400;
            position.height = 300;

            popup.position = position;

            popup.ShowPopup();
        }

        private void UnregisterCallbacks()
        {
            filterButton.clickable.clickedWithEventInfo -= OnClickFilterButton;

            root.Q<VisualElement>("main-container").UnregisterCallback<PointerDownEvent>(OnClickMainContainer);

            toolTypeField.UnregisterCallback<ChangeEvent<System.Enum>>(e => SwitchTool((ToolType)e.newValue));
        }

        private void SwitchTool(ToolType toolType)
        {
            currentTool?.OnDestroy();

            toolTypeField.value = toolType;
            currentTool = toolFactory.Create(toolType);

            currentToolPanel?.DestroyPanel();
            currentToolPanel = toolPanelFactory.Create(toolType);
        }

        public override void OnExit()
        {
            tilemap.OnRefresh -= OnRefresh;

            layerPanel.Free();

            UnregisterCallbacks();

            Undo.undoRedoPerformed -= OnUndoRedo;
            UnregisterEvents();
            SceneView.RepaintAll();
            currentTool?.OnDestroy();
        }

        public override void OnSceneGUI()
        {
            eventHandler.ProcessEvents(Event.current);

            Render();
            DrawingUtility.DrawGrid(tilemap);
        }

        private void PopulateTileButtons()
        {
            RemoveNullTilemapTiles();

            var tileContainer = root.Q<VisualElement>("paint-tile-container");
            tileContainer.Clear();

            buttonTilePairs.Clear();

            foreach (var tile in tilemap.tiles)
            {
                if (tile?.prefab == null) continue;

                var tileButtonParent = new VisualElement();
                tileButtonParent.AddToClassList("tile-button-parent");
                tileContainer.Add(tileButtonParent);

                var tileButton = new Button();
                tileButton.AddToClassList("tile-button");

                var tex = new Texture2D(1, 1);
                tex.SetPixel(0, 0, new Color(1.0f, 1.0f, 1.0f, 0.4f));
                tex.Apply();

                tileButton.style.backgroundImage = new StyleBackground(tex);
                tileButton.style.unityBackgroundImageTintColor = tile.category != null ? new StyleColor(tile.category.color) : new Color(0.5f, 0.5f, 0.5f, 0.0f);
                tileButtonParent.Add(tileButton);

                buttonTilePairs.Add(tileButton, tile);

                var icon = new Image
                {
                    image = tile.thumbnail
                };

                icon.AddToClassList("tile-button-icon");
                tileButton.Add(icon);

                var label = new Label();
                label.AddToClassList("tile-button-label");
                label.text = tile.prefab.name;
                tileButtonParent.Add(label);

                tileButton.RegisterCallback<PointerDownEvent>(e =>
                {
                    OnClickTileButton(tile, tileButton, e);
                    e.StopImmediatePropagation();
                    e.StopPropagation();
                }, TrickleDown.TrickleDown);

                tileButton.AddManipulator(new ContextualMenuManipulator(e => 
                {
                    e.menu.AppendAction("Edit", OnEditTileAction, DropdownMenuAction.AlwaysEnabled, tile);
                    e.menu.AppendAction("Remove", OnRemoveTileAction, DropdownMenuAction.AlwaysEnabled, tile);
                }));
            }
        }

        private void OnEditTileAction(DropdownMenuAction action)
        {
            Assert.IsTrue(action.userData is Tile);
            var tile = (Tile)action.userData;

            var popup = EditorWindow.CreateWindow<EditTilePopup>("Edit tile");
            popup.titleContent = new GUIContent($"Edit '{tile.prefab.name}' tile");
            popup.Tile = tile;
            popup.Tilemap = tilemap;

            var position = popup.position;

            position.center = new Vector2(
                Screen.currentResolution.width / 2.0f,
                Screen.currentResolution.height / 2.0f);

            position.width = 400;
            position.height = 150;

            popup.position = position;

            popup.ShowPopup();
        }

        private void OnRemoveTileAction(DropdownMenuAction action)
        {
            Assert.IsTrue(action.userData is Tile);
            var tile = (Tile)action.userData;

            selectedTiles.ForEach(t => tilemap.tiles.Remove(t));

            if (tile != null)
                tilemap.tiles.Remove(tile);

            PopulateTileButtons();
            FilterTileButtons(tilemap.searchValue, tilemap.selectedCategory);
        }

        private void RemoveNullTilemapTiles()
        {
            for (int i = tilemap.tiles.Count - 1; i >= 0; --i)
            {
                if (tilemap.tiles[i].prefab == null)
                    tilemap.tiles.RemoveAt(i);
            }
        }

        private void OnClickTileButton(Tile tile, Button button, PointerDownEvent e)
        {
            if (e.imguiEvent.button == 1)
                return;

            if (!e.imguiEvent.shift)
                DeselectTiles();

            if (selectedButtonTilePairs.ContainsKey(button)) return;
            if (selectedTiles.Contains(tile)) return;

            button.AddToClassList("selected-tile-button");
            selectedButtonTilePairs.Add(button, tile);
            selectedTileButtons.Add(button);
            selectedTiles.Add(tile);
            tilemap.selectedTile = tile;

            selectedTileButton = button;
            selectedTileButton.AddToClassList("selected-tile-button");

            UpdateSelectedTileMeshPreview();
        }

        private void DeselectTiles()
        {
            foreach (var button in selectedButtonTilePairs.Keys)
            {
                button.RemoveFromClassList("selected-tile-button");
            }

            selectedTileButtons.ForEach(b => b.RemoveFromClassList("selected-tile-button"));
            selectedTileButtons.Clear();
            selectedButtonTilePairs.Clear();
            selectedTiles.Clear();

            tilemap.tilePreviewMesh = null;
        }

        private void UpdateSelectedTileMeshPreview()
        {
            var oldPosition = tilemap.selectedTile.prefab.transform.position;
            tilemap.selectedTile.prefab.transform.position = Vector3.zero;
            tilemap.selectedTile.prefab.transform.rotation = Quaternion.identity;

            var meshFilters = tilemap.selectedTile.prefab.GetComponentsInChildren<MeshFilter>(true);
            var combineInstances = new CombineInstance[meshFilters.Length];

            for (int i = 0; i < meshFilters.Length; ++i)
            {
                combineInstances[i].mesh = meshFilters[i].sharedMesh;
                combineInstances[i].transform = meshFilters[i].transform.localToWorldMatrix;
            }

            tilemap.tilePreviewMesh = new Mesh();
            tilemap.tilePreviewMesh.CombineMeshes(combineInstances);

            tilemap.selectedTile.prefab.transform.position = oldPosition;
        }

        private void OnClickMainContainer(PointerDownEvent e)
        {
            if (isHoveringOverRemoveButton) return;

            DeselectTiles();
        }

        private void Render()
        {
            currentTool.OnRender(Event.current);
            HandleUtility.Repaint();
            GetTilePreviews();
        }

        private void GetTilePreviews()
        {
            foreach (var pair in buttonTilePairs)
            {
                var button = pair.Key;
                var tile = pair.Value;

                if (tile.thumbnail == null && button.Q<Image>().image == null)
                {
                    var preview = AssetPreview.GetAssetPreview(tile.prefab);
                    if (preview == null) continue;

                    tile.thumbnail = preview;
                    button.Q<Image>().image = preview;
                }
            }
        }

        private void RegisterEvents()
        {
            eventHandler.OnKeyDown += OnKeyDown;
            eventHandler.OnMouseMove += OnMouseMove;
            eventHandler.OnSceneGUI += OnSceneGUI;
            eventHandler.OnMouseDown += OnMouseDown;
            RegisterCurrentToolEvents();
        }

        private void UnregisterEvents()
        {
            UnregisterCurrentToolEvents();
            eventHandler.OnMouseDown -= OnMouseDown;
            eventHandler.OnSceneGUI -= OnSceneGUI;
            eventHandler.OnMouseMove -= OnMouseMove;
            eventHandler.OnKeyDown -= OnKeyDown;
        }

        private void RegisterCurrentToolEvents()
        {
            eventHandler.OnKeyDown += e => currentTool.OnKeyDown(e);
            eventHandler.OnKeyUp += e => currentTool.OnKeyUp(e);
            eventHandler.OnMouseUp += e => currentTool.OnMouseUp(e);
            eventHandler.OnMouseDown += e => currentTool.OnMouseDown(e);
            eventHandler.OnMouseDrag += e => currentTool.OnMouseDrag(e);
        }

        private void UnregisterCurrentToolEvents()
        {
            eventHandler.OnMouseDrag -= e => currentTool.OnMouseDrag(e);
            eventHandler.OnMouseDown -= e => currentTool.OnMouseDown(e);
            eventHandler.OnMouseUp -= e => currentTool.OnMouseUp(e);
            eventHandler.OnKeyUp -= e => currentTool.OnKeyUp(e);
            eventHandler.OnKeyDown -= e => currentTool.OnKeyDown(e);
        }

        private void OnMouseMove(Event e)
        {
            if (shouldFocusOnSceneView)
            {
                FocusOnSceneView();
            }

            tilemap.selectedGameObject = HandleUtility.PickGameObject(e.mousePosition, true);
            currentTool.OnMouseMove(e);
        }

        private void OnSceneGUI(Event e)
        {
            currentTool.OnRender(e);
            HandleUtility.Repaint();
        }

        private void FocusOnSceneView()
        {
            if (SceneView.sceneViews.Count > 0)
            {
                var sceneView = (SceneView)SceneView.sceneViews[0];
                sceneView.Focus();
            }
        }

        private void OnKeyDown(Event e)
        {
            if (e.keyCode == tilemap.raiseFloorKey)
            {
                tilemap.floorIndex++;
                e.Use();
            }
            else if (e.keyCode == tilemap.lowerFloorKey)
            {
                tilemap.floorIndex--;
                e.Use();
            }
            else if (e.keyCode == tilemap.brushKey)
            {
                SwitchTool(ToolType.Brush);
                e.Use();
            }
            else if (e.keyCode == tilemap.eraserKey)
            {
                SwitchTool(ToolType.Eraser);
                e.Use();
            }
            else if (e.keyCode == tilemap.rectangleToolKey)
            {
                SwitchTool(ToolType.RectangleTool);
                e.Use();
            }
            else if (e.keyCode == tilemap.selectionBoxKey)
            {
                SwitchTool(ToolType.SelectionBox);
                e.Use();
            }
        }

        private void RemoveNullTiles()
        {
            if (tilemap == null) return;

            tilemap.instantiatedTiles.Clear();
            int childCount = tilemap.transform.childCount;

            for (int i = 0; i < childCount; i++)
            {
                tilemap.instantiatedTiles.Add(tilemap.transform.GetChild(i).gameObject);
            }
        }

        private void OnMouseDown(Event e)
        {
            if (!e.control || Input.GetKey(KeyCode.LeftControl)) return;
            if (e.alt || Input.GetKey(KeyCode.LeftAlt)) return;

            FocusOnHoveredGameObject(e.mousePosition);
        }

        private void FocusOnHoveredGameObject(Vector2 mousePosition) 
        {
            var ray = HandleUtility.GUIPointToWorldRay(mousePosition);
            var hit = FabgridRaycaster.InaccurateMeshRaycast(ray, tilemap);

            if (hit.transform != null)
            {
                SceneView.lastActiveSceneView.Frame(
                    new Bounds()
                    {
                        center = hit.transform.position
                    },
                    instant: false);
            }
        }

        private void OnUndoRedo()
        {
            RemoveNullTiles();

            foreach (var layer in GameObject.FindObjectsOfType<LayerBehaviour>())
            {
                if (layer.tilemap == tilemap && !tilemap.layers.Contains(layer.gameObject))
                {
                    tilemap.layers.Add(layer.gameObject);
                }
            }

            layerPanel.PopulateLayerItems();
        }

        private void OnRefresh()
        {
            PopulateTileButtons();
        }
    }
}

#endif