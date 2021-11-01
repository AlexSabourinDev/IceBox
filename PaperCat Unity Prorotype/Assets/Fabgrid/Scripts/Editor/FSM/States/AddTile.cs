using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class AddTile : State
    {
        private readonly Tilemap3D tilemap;
        private readonly SerializedObject serializedTilemap;
        private Vector3Field tileSizeField;
        private Vector3Field tileOffsetField;
        private SerializedProperty tileSizeProperty;
        private SerializedProperty tileOffsetProperty;
        private ObjectField tilePrefabField;
        private TextField prefabsPathField;
        private EnumField addOptionField;
        private EnumField sizeCalculationField;
        private Button addTileButton;
        private Button folderPanelButton;
        private Button saveTileSetButton;
        private Button loadTileSetButton;
        private VisualElement pathContainer;

        public AddTile(VisualElement root, Tilemap3D tilemap) : base(root)
        {
            this.tilemap = tilemap;
            this.tilemap.newTile = new Tile();
            this.serializedTilemap = new SerializedObject(tilemap);
        }

        public override void OnEnter()
        {
            var mainPanelHeader = root.Q<Label>("main-panel-header");
            mainPanelHeader.text = "Add Tile";

            SetupControls();
            RegisterCallbacks();

            var addTilePanel = root.Q("add-tile-panel");
            StylingUtility.AddClassToAllOfType<Label>(addTilePanel, "field-label");

            // Exclude styling from the label inside the prefab display field
            tilePrefabField.
                Q(null, "unity-object-field-display")
                .Q<Label>().RemoveFromClassList("field-label");

            pathContainer = addTilePanel.Q("path-container");
            pathContainer.style.flexDirection = FlexDirection.Row;

            folderPanelButton = pathContainer.Q<Button>("folder-panel-button");
            folderPanelButton.clickable.clicked += () => 
            {
                var absolutePath = EditorUtility.OpenFolderPanel("Select a folder to load prefabs from", "", "");
                var relativePath = PathUtility.AbsoluteToRelative(absolutePath);

                if (relativePath != null)
                {
                    relativePath = relativePath.Replace("Assets/", "");
                    tilemap.prefabsPath = relativePath;
                }
                else
                {
                    FabgridLogger.LogWarning("Invalid path");
                }
            };

            UpdateAddOptionField(tilemap.addOption);
            UpdateSizeCalculationField(tilemap.newTile.sizeCalculationOption);
        }

        private void OnClickLoadTileSetButton()
        {
            var absolutePath = EditorUtility.OpenFilePanel("Select what tile set to load", "", "asset");
            if (absolutePath == null) return;
            if (absolutePath == "") return;

            var relativePath = PathUtility.AbsoluteToRelative(absolutePath);

            var tileSet = AssetDatabase.LoadAssetAtPath(relativePath, typeof(TileSet)) as TileSet;
            if (tileSet == null)
            {
                FabgridLogger.LogWarning($"Could not load a tile set from the path: '{relativePath}'");
                return;
            }

            if (tilemap.tiles.Any())
            {
                const int add = 0;
                const int cancel = 1;
                const int replace = 2;

                var choice = EditorUtility.DisplayDialogComplex("Load Tile Set",
                    "There already exists some tiles on this component. Would you like to add these to the existing tiles or replace the tiles completely?",
                    "Add",
                    "Cancel",
                    "Replace");

                if (choice == cancel) return;

                if (choice == add)
                {
                    tileSet.tiles?.ToList().ForEach(tile => AddTileIfNotDuplicate(tile));
                }
                else if (choice == replace)
                {
                    tilemap.tiles?.Clear();
                    tileSet.tiles?.ToList().ForEach(tile => AddTileIfNotDuplicate(tile));
                }
            }
            else 
            {
                tileSet.tiles?.ToList().ForEach(tile => AddTileIfNotDuplicate(tile));
            }
        }

        private void OnClickSaveTileSetButton()
        {
            var path = EditorUtility.SaveFilePanelInProject("Select where to save your tile set", "TileSet", "asset", "Select where to save...");
            if (path == null) return;
            if (path == "") return;

            var tileSet = TileSet.CreateInstance<TileSet>();
            tileSet.tiles = tilemap.tiles.ToArray();

            AssetDatabase.CreateAsset(tileSet, path);
        }

        public override void OnExit()
        {
            UnregisterCallbacks();
        }

        private void SetupControls()
        {
            addTileButton = root.Q<Button>("add-tile-button");

            tileSizeField = root.Q<Vector3Field>("tile-size-vector3-field");
            tileSizeProperty = serializedTilemap.FindProperty("customSize");
            tileSizeField.BindProperty(tileSizeProperty);

            tileOffsetField = root.Q<Vector3Field>("tile-offset-field");
            tileOffsetProperty = serializedTilemap.FindProperty("newTile.offset");
            tileOffsetField.BindProperty(tileOffsetProperty);

            tilePrefabField = root.Q<ObjectField>("tile-prefab-field");
            tilePrefabField.objectType = typeof(GameObject);

            var tilePrefabProperty = serializedTilemap.FindProperty("newTile.prefab");
            tilePrefabField.BindProperty(tilePrefabProperty);

            addOptionField = root.Q<EnumField>("add-option-field");
            addOptionField.Bind(serializedTilemap);

            sizeCalculationField = root.Q<EnumField>("size-calculation-field");
            sizeCalculationField.Bind(serializedTilemap);

            prefabsPathField = root.Q<TextField>("prefabs-path-field");
            prefabsPathField.Bind(serializedTilemap);

            saveTileSetButton = root.Q<Button>("save-tile-set-button");
            loadTileSetButton = root.Q<Button>("load-tile-set-button");
        }

        private void RegisterCallbacks()
        {
            addOptionField.RegisterCallback<ChangeEvent<System.Enum>>(e =>
            {
                if (e.newValue == null) return;
                UpdateAddOptionField((TileAddOption)e.newValue);
            });

            sizeCalculationField.RegisterCallback<ChangeEvent<System.Enum>>(e =>
            {
                if (e.newValue == null) return;
                UpdateSizeCalculationField((SizeCalculationOption)e.newValue);
            });

            addTileButton.clickable.clicked += OnClickAddTileButton;
            saveTileSetButton.clickable.clicked += OnClickSaveTileSetButton;
            loadTileSetButton.clickable.clicked += OnClickLoadTileSetButton;
        }

        private void UnregisterCallbacks()
        {
            loadTileSetButton.clickable.clicked -= OnClickLoadTileSetButton;
            saveTileSetButton.clickable.clicked -= OnClickSaveTileSetButton;
            addTileButton.clickable.clicked -= OnClickAddTileButton;

            sizeCalculationField.UnregisterCallback<ChangeEvent<System.Enum>>(e => UpdateSizeCalculationField((SizeCalculationOption)e.newValue));
            addOptionField.UnregisterCallback<ChangeEvent<System.Enum>>(e => UpdateAddOptionField((TileAddOption)e.newValue));
        }

        private void OnClickAddTileButton()
        {
            if (tilemap.addOption == TileAddOption.Single)
            {
                AddSingleTile();
            }
            else if (tilemap.addOption == TileAddOption.Multiple)
            {
                AddMultipleTiles();
            }
        }

        private void AddSingleTile()
        {
            if (tilemap.newTile.prefab != null)
            {
                if (CanCalculateSizeOf(tilemap.newTile.prefab))
                {
                    tilemap.newTile.customSize = tilemap.customSize;
                    AddTileIfNotDuplicate(tilemap.newTile);
                }
            }
        }

        private void AddMultipleTiles()
        {
            var assetPath = UnityEngine.Application.dataPath;
            assetPath += "/" + tilemap.prefabsPath;

            if (!Directory.Exists(assetPath))
            {
                Debug.LogWarning($"Fabgrid Warning: Could not find the path '{assetPath}'");
                return;
            }

            var prefabPaths = Directory.GetFiles(assetPath, "*.prefab");

            for (int i = 0; i < prefabPaths.Length; ++i)
            {
                var relativePath = PathUtility.AbsoluteToRelative(prefabPaths[i]);
                var asset = AssetDatabase.LoadAssetAtPath<GameObject>(relativePath);

                if (asset != null)
                {
                    if (CanCalculateSizeOf(asset))
                    {
                        var newTile = new Tile
                        {
                            prefab = asset,
                            offset = tilemap.newTile.offset,
                            sizeCalculationOption = tilemap.newTile.sizeCalculationOption
                        };

                        AddTileIfNotDuplicate(newTile);
                    }
                }
            }
        }

        private void AddTileIfNotDuplicate(Tile tile)
        {
            var alreadyContainsTile = tilemap.tiles.Any(t => t.prefab.name == tile.prefab.name);
            if (!alreadyContainsTile)
            {
                tilemap.tiles.Add(tile);
            }
            else 
            {
                FabgridLogger.LogWarning($"Skipped adding the tile '{tile.prefab.name}' because it already exists on the component '{tilemap.gameObject.name}'.");
            }
        }

        private void UpdateAddOptionField(TileAddOption tileAddOption)
        {
            if (tileAddOption == TileAddOption.Single)
            {
                tilePrefabField.Show();
                pathContainer.Hide();
            }
            else if (tileAddOption == TileAddOption.Multiple)
            {
                pathContainer.Show();
                tilePrefabField.Hide();
            }
        }

        private void UpdateSizeCalculationField(SizeCalculationOption sizeCalculationOption)
        {
            if (sizeCalculationOption == SizeCalculationOption.Custom)
            {
                tileSizeField.Show();
            }
            else
            {
                tileSizeField.Hide();
            }
        }

        private bool CanCalculateSizeOf(GameObject prefab)
        {
            switch (tilemap.newTile.sizeCalculationOption)
            {
                case SizeCalculationOption.Collider:
                    if (FabgridUtility.HasAnyComponent<Collider>(prefab)) return true;
                    break;

                case SizeCalculationOption.Custom:
                    return true;

                case SizeCalculationOption.Mesh:
                    if (FabgridUtility.HasAnyComponent<Renderer>(prefab)) return true;
                    break;

                default:
                    return false;
            }

            FabgridLogger.LogWarning($"Could not determine the size of the prefab '{prefab.name}' using '{tilemap.newTile.sizeCalculationOption}'");
            return false;
        }
    }
}