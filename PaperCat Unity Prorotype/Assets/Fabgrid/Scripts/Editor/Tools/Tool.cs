#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public abstract class Tool
    {
        protected Tilemap3D tilemap;
        protected TilePositionCalculator positionCalculator;
        protected ToolType toolType;
        private Camera camera;

        public ToolType Type { get { return toolType; } }

        public const float MAX_PLACE_DISTANCE = 140f;

        protected Tool(Tilemap3D tilemap)
        {
            this.tilemap = tilemap;
            positionCalculator = CreateTilePositionCalculator();
        }

        private TilePositionCalculator CreateTilePositionCalculator()
        {
            switch (tilemap.snapOption)
            {
                case SnapOption.Adaptive:
                    return new AdaptiveTilePositionCalculator(tilemap);

                case SnapOption.Floor:
                    return new FloorTilePositionCalculator(tilemap);

                case SnapOption.Mesh:
                    return new MeshTilePositionCalculator(tilemap);

                case SnapOption.Grid:
                    return new GridTilePositionCalculator(tilemap);

                default:
                    return new AdaptiveTilePositionCalculator(tilemap);
            }
        }

        public void CreateTileAt(Vector3 position)
        {
            if (position.IsInfinity()) return;

            if (camera == null)
            {
                camera = Camera.current;
            }

            if (Vector3.Distance(camera.transform.position, position) > MAX_PLACE_DISTANCE) return;

            GameObject tile = (GameObject)PrefabUtility.InstantiatePrefab(tilemap.selectedTile.prefab);
            if (tilemap.selectedLayer != null) tile.transform.SetParent(tilemap.selectedLayer.transform);
            else tile.transform.SetParent(tilemap.transform);
            tile.transform.position = position;
            tile.transform.rotation = tilemap.tileRotation;
            Undo.RegisterCreatedObjectUndo(tile, "Placed tile");
            tilemap.instantiatedTiles.Add(tile);
        }

        public virtual void OnDestroy()
        {
        }

        public virtual void OnMouseDown(Event e)
        {
        }

        public virtual void OnMouseUp(Event e)
        {
        }

        public virtual void OnMouseDrag(Event e)
        {
        }

        public virtual void OnMouseMove(Event e)
        {
        }

        public virtual void OnRender(Event e)
        {
        }

        public virtual void OnKeyDown(Event e)
        {
        }

        public virtual void OnKeyUp(Event e)
        {
        }
    }
}

#endif