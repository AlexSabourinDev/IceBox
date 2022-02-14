using System.Collections.Generic;
using UnityEngine;
using System.Linq;
using System;

#if UNITY_EDITOR

using UnityEditor;

#endif

namespace Fabgrid
{
    [ExecuteInEditMode]
    public class Tilemap3D : MonoBehaviour
    {
        public TileAddOption addOption = TileAddOption.Single;
        public Tile currentlyEditing;
        public Vector3 customSize;
        public int floorIndex;
        public float floorSpacing = 1f;
        public Color gridColor = Color.white;
        public int gridSize = 100;
        public float gridSpacing = 1f;
        public List<GameObject> instantiatedTiles = new List<GameObject>();
        public Tile newTile;
        public string prefabsPath;
        public float rotationStepAngle = 90f;
        public Tile selectedTile = null;
        public SnapOption snapOption = SnapOption.Adaptive;
        public ToolType toolType = ToolType.Brush;
        public TileSet tileSet = null;
        public Quaternion tileRotation = Quaternion.identity;
        public List<Tile> tiles = new List<Tile>();
        public GameObject selectedGameObject;
        public Mesh tilePreviewMesh;
        public Material tilePreviewMaterial;

        public GameObject selectedLayer;
        public List<GameObject> layers = new List<GameObject>();

        public OffsetOption rectangleToolOffsetOption = OffsetOption.Relative;
        public Vector2 rectangleOffsetMultiplier = Vector2.one;

        public KeyCode selectionBoxKey = KeyCode.B;
        public KeyCode brushKey = KeyCode.F1;
        public KeyCode eraserKey = KeyCode.F2;
        public KeyCode rectangleToolKey = KeyCode.F3;
        public KeyCode raiseFloorKey = KeyCode.E;
        public KeyCode lowerFloorKey = KeyCode.Q;
        public KeyCode rotateTileAroundYKey = KeyCode.X;
        public KeyCode rotateTileAroundZKey = KeyCode.Z;
        public KeyCode resetTileRotationKey = KeyCode.R;
        public const KeyCode focusOnObjectKey = KeyCode.LeftControl;

        public string searchValue = "";

        public const float MIN_GRID_SPACING = 0.0001f;

        public List<Category> categories = new List<Category>();
        public Category selectedCategory = null;
        public Action OnRefresh { get; set; }

        public Vector3 GetFloorPosition()
        {
            return transform.position + (Vector3.up * floorIndex * floorSpacing);
        }

        public void Refresh()
        {
            OnRefresh.Invoke();
        }

#if UNITY_EDITOR

        public Vector3 MouseToGridPlanePosition(Vector2 mousePosition)
        {
            Ray ray = HandleUtility.GUIPointToWorldRay(mousePosition);
            var floorPosition = GetFloorPosition();
            Plane plane = new Plane(Vector3.up, floorPosition);

            if (plane.Raycast(ray, out float enter))
            {
                return ray.GetPoint(enter);
            }

            return Vector3.negativeInfinity;
        }

        public Vector3 MouseToGridPosition(Vector2 mousePosition)
        {
            var position = MouseToGridPlanePosition(mousePosition);
            return WorldToGridPosition(position);
        }

        public Vector3 WorldToGridPosition(Vector3 position)
        {
            var spacing = Mathf.Max(gridSpacing, Tilemap3D.MIN_GRID_SPACING);

            var halfGridSpacing = new Vector3(1f, 0f, 1f) * spacing * 0.5f;
            var offset = FabgridMath.Repeat(transform.position, spacing) + halfGridSpacing;

            position -= offset;

            position = new Vector3(
                Mathf.Round(position.x / spacing) * spacing,
                position.y,
                Mathf.Round(position.z / spacing) * spacing);

            position += offset;

            return position;
        }

#endif

        private void OnEditorUpdate()
        {
            if (transform.hasChanged)
            {
                OnTransformChanged();
                transform.hasChanged = false;
            }
        }

        private void OnTransformChanged()
        {
            transform.rotation = Quaternion.identity;
            transform.localScale = Vector3.one;
        }

        private void Update()
        {
            if (Application.isEditor)
            {
                OnEditorUpdate();
            }
        }

        public bool HasSelectedVisibleLayer()
        {
            return selectedLayer != null && selectedLayer.activeSelf;
        }
    }
}