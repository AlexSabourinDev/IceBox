#if UNITY_EDITOR

using UnityEngine;

namespace Fabgrid
{
    public class RectangleTool : Tool
    {
        private Vector3 mouseDownPosition;
        private Vector3 currentFloorPosition;
        private bool isDragging = false;
        private int horizontalCount;
        private int verticalCount;
        private readonly TilePreviewRenderer tilePreviewRenderer;
        private readonly TileRotator tileRotator;

        public RectangleTool(Tilemap3D tilemap) : base(tilemap)
        {
            toolType = ToolType.RectangleTool;
            tilePreviewRenderer = new TilePreviewRenderer(tilemap);
            tileRotator = new TileRotator(tilemap);
        }

        public override void OnMouseDown(Event e)
        {
            if (!EvaluatePreConditions(e)) return;

            mouseDownPosition = positionCalculator.GetPosition(e.mousePosition);
            isDragging = true;
            e.Use();
        }

        private bool EvaluatePreConditions(Event e)
        {
            if (e.button != 0) return false;
            if (e.alt || Input.GetKey(KeyCode.LeftAlt)) return false;
            if (e.control || Input.GetKey(KeyCode.LeftControl)) return false;

            //if (!tilemap.HasSelectedVisibleLayer())
            //{
            //    FabgridLogger.LogInfo("Cannot place tiles without selecting a visible layer first.");
            //    return false;
            //}

            return true;
        }

        public override void OnMouseUp(Event e)
        {
            if (e.button != 0) return;

            PlaceTiles();
            Reset();
        }

        private void PlaceTiles()
        {
            if (!isDragging) return;
            if (tilemap.selectedTile == null || tilemap.selectedTile.prefab == null) return;

            var offset = currentFloorPosition - mouseDownPosition;

            for (int x = 0; x < horizontalCount; x++)
            {
                for (int z = 0; z < verticalCount; z++)
                {
                    var position = GetDuplicatePosition(new Vector3Int(x, 0, z), offset);
                    CreateTileAt(position);
                }
            }
        }

        private void Reset()
        {
            isDragging = false;
            horizontalCount = 0;
            verticalCount = 0;
        }

        public override void OnMouseDrag(Event e)
        {
            if (e.button != 0) return;

            currentFloorPosition = tilemap.MouseToGridPlanePosition(e.mousePosition);

            var offset = currentFloorPosition - mouseDownPosition;

            var spacing = GetSpacing();

            horizontalCount = Mathf.CeilToInt(Mathf.Abs(offset.x) / spacing.x);
            verticalCount = Mathf.CeilToInt(Mathf.Abs(offset.z) / spacing.y);
        }

        public override void OnKeyDown(Event e)
        {
            tileRotator.OnKeyDown(e);

            if (e.keyCode == KeyCode.Escape)
            {
                Reset();
                e.Use();
            }
        }

        private Vector2 GetSpacing()
        {
            if (tilemap.selectedTile?.prefab == null) return default;

            if (tilemap.rectangleToolOffsetOption == OffsetOption.Grid)
            {
                return new Vector2(tilemap.gridSpacing * tilemap.rectangleOffsetMultiplier.x,
                    tilemap.gridSpacing * tilemap.rectangleOffsetMultiplier.y);
            }
            else if (tilemap.rectangleToolOffsetOption == OffsetOption.Relative)
            {
                if (tilemap.selectedTile?.prefab != null)
                {
                    var bounds = tilemap.selectedTile.GetWorldBounds(Vector3.zero, tilemap.tileRotation, tilemap);
                    return new Vector2(bounds.size.x * tilemap.rectangleOffsetMultiplier.x
                        , bounds.size.z * tilemap.rectangleOffsetMultiplier.y);
                }
            }

            throw new System.Exception($"Fabgrid Error: Unknown tool type {tilemap.rectangleToolOffsetOption}");
        }

        public override void OnRender(Event e)
        {
            if (isDragging)
            {
                DrawPreviewDuplicates();
            }
            else
            {
                tilePreviewRenderer.Render(positionCalculator.GetPosition(e.mousePosition));
            }
        }

        private void DrawPreviewDuplicates()
        {
            var offset = currentFloorPosition - mouseDownPosition;

            for (int x = 0; x < horizontalCount; ++x)
            {
                for (int z = 0; z < verticalCount; z++)
                {
                    var position = GetDuplicatePosition(new Vector3Int(x, 0, z), offset);

                    tilemap.tilePreviewMaterial.SetPass(0);
                    Graphics.DrawMeshNow(tilemap.tilePreviewMesh,
                        position,
                        tilemap.tileRotation);
                }
            }
        }

        private Vector3 GetDuplicatePosition(Vector3Int coordinate, Vector3 offset)
        {
            var sign = new Vector3(Mathf.Sign(offset.x), 1f, Mathf.Sign(offset.z));
            var spacing = GetSpacing();

            var position = mouseDownPosition +
                new Vector3(coordinate.x * spacing.x * sign.x,
                0f,
                coordinate.z * spacing.y * sign.z);

            if (tilemap.rectangleToolOffsetOption != OffsetOption.Relative)
            {
                position = tilemap.WorldToGridPosition(position);
            }

            return position;
        }
    }
}

#endif