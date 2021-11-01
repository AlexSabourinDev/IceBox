using NUnit.Framework;
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Paint : State
    {
        private readonly Tilemap3D tilemap;
        private readonly SerializedObject serializedTilemap;
        private Button selectedTileButton;
        private Button deleteTileButton;
        private readonly Dictionary<Button, Tile> selectedButtonTilePairs;
        private readonly Dictionary<Button, Tile> buttonTilePairs;
        private Tool currentTool;
        private ToolPanel currentToolPanel;
        private readonly EventHandler eventHandler;
        private readonly ToolFactory toolFactory;
        private readonly ToolPanelFactory toolPanelFactory;
        private EnumField toolTypeField;
        private VisualElement toolPanel;
        private TextField searchField;
        private bool shouldFocusOnSceneView = false;
        private bool isHoveringOverRemoveButton = false;

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
            tilemap.selectedTile = null;

            PopulateTileButtons();

            var mainPanelHeader = root.Q<Label>("main-panel-header");
            mainPanelHeader.text = "Paint";

            toolPanel = root.Q("tool-panel");

            SetupControls();

            StylingUtility.AddClassToAllOfType<Label>(toolPanel, "field-label");

            Undo.undoRedoPerformed += OnUndoRedo;
            RegisterEvents();
            SceneView.RepaintAll();

            root.RegisterCallback<MouseEnterEvent>(_ => shouldFocusOnSceneView = false);
            root.RegisterCallback<MouseLeaveEvent>(_ => shouldFocusOnSceneView = true);

            deleteTileButton.RegisterCallback<MouseEnterEvent>(_ => { isHoveringOverRemoveButton = true; });
            deleteTileButton.RegisterCallback<MouseLeaveEvent>(_ => { isHoveringOverRemoveButton = false; });

            ShowTileButtonsThatContains(tilemap.searchValue);
        }

        private void SetupControls()
        {
            deleteTileButton = root.Q<Button>("delete-tile-button");
            deleteTileButton.Hide();

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

            searchField.RegisterValueChangedCallback(e => ShowTileButtonsThatContains(e.newValue));

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

        private void ShowTileButtonsThatContains(string name)
        {
            if (name == null || name?.Length == 0)
            {
                ShowAllTileButtons();
                return;
            }

            foreach (var buttonParent in root.Query(null, "tile-button-parent").ToList())
            {
                var label = buttonParent.Q<Label>();

                if (label.text.IndexOf(name, System.StringComparison.OrdinalIgnoreCase) >= 0)
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
            deleteTileButton.clickable.clickedWithEventInfo += e =>
            {
                OnClickDeleteButton();
                e.StopPropagation();
                e.StopImmediatePropagation();
            };

            toolTypeField.RegisterCallback<ChangeEvent<System.Enum>>(e =>
            {
                if (e.newValue == null) return;
                SwitchTool((ToolType)e.newValue);
            });

            root.Q<VisualElement>("main-container").
                RegisterCallback<PointerDownEvent>(_ => OnClickMainContainer(),
                TrickleDown.NoTrickleDown);
        }

        private void UnregisterCallbacks()
        {
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
                    OnClickTileButton(tile, tileButton, e.imguiEvent.shift);
                    e.StopImmediatePropagation();
                    e.StopPropagation();
                }, TrickleDown.TrickleDown);
            }
        }

        private void RemoveNullTilemapTiles()
        {
            for (int i = tilemap.tiles.Count - 1; i >= 0; --i)
            {
                if (tilemap.tiles[i].prefab == null)
                {
                    tilemap.tiles.RemoveAt(i);
                }
            }
        }

        private void OnClickTileButton(Tile tile, Button button, bool shift)
        {
            if (!shift)
            {
                DeselectTiles();
            }

            button.AddToClassList("selected-tile-button");
            selectedButtonTilePairs.Add(button, tile);
            tilemap.selectedTile = tile;

            selectedTileButton = button;
            selectedTileButton.AddToClassList("selected-tile-button");
            deleteTileButton.Show();

            UpdateSelectedTileMeshPreview();
        }

        private void DeselectTiles()
        {
            foreach (var button in selectedButtonTilePairs.Keys)
            {
                button.RemoveFromClassList("selected-tile-button");
            }

            selectedButtonTilePairs.Clear();
        }

        private void UpdateSelectedTileMeshPreview()
        {
            var oldPosition = tilemap.selectedTile.prefab.transform.position;
            tilemap.selectedTile.prefab.transform.position = Vector3.zero;

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

        private void OnClickMainContainer()
        {
            if (isHoveringOverRemoveButton) return;

            DeselectTiles();
            deleteTileButton.Hide();
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

        private void OnClickDeleteButton()
        {
            Assert.IsTrue(selectedButtonTilePairs.Values.Count > 0, "Fabgrid Error: selectedButtonTilePairs is empty.");

            foreach (var tile in selectedButtonTilePairs.Values)
            {
                tilemap.tiles.Remove(tile);
            }

            tilemap.selectedTile = null;
            selectedTileButton = null;

            PopulateTileButtons();
            ShowTileButtonsThatContains(tilemap.searchValue);

            deleteTileButton.Hide();
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
        }
    }
}