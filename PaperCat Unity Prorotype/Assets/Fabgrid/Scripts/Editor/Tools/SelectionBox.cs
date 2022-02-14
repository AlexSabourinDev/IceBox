#if UNITY_EDITOR

using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    internal class Selection
    {
        public GameObject gameObject;
        public List<Renderer> renderers;
        public List<Material> materials;

        public Selection()
        {
            renderers = new List<Renderer>();
            materials = new List<Material>();
        }
    }

    public class SelectionBox : Tool
    {
        private bool isDragging = false;
        private Vector2 mouseDownPosition;
        private Rect selectionBoxRect;
        private Material selectMaterial;
        private readonly List<Selection> selections;
        private readonly GUIStyle boxStyle;
        private Texture2D boxBackground;

        public SelectionBox(Tilemap3D tilemap) : base(tilemap)
        {
            toolType = ToolType.SelectionBox;
            selections = new List<Selection>();
            LoadResources();

            boxStyle = new GUIStyle();
            boxStyle.normal.background = boxBackground;
            boxStyle.border.left = 1;
            boxStyle.border.right = 1;
            boxStyle.border.top = 1;
            boxStyle.border.bottom = 1;
        }

        private void LoadResources()
        {
            var fabgridFolder = PathUtility.GetFabgridFolder();
            selectMaterial = AssetDatabase.LoadAssetAtPath<Material>($"{fabgridFolder}/Materials/FabgridSelectMaterial.mat");
            boxBackground = AssetDatabase.LoadAssetAtPath<Texture2D>($"{fabgridFolder}/Textures/SelectionBox.png");
        }

        public override void OnMouseDown(Event e)
        {
            if (e.button != 0) return;
            //if (!tilemap.HasSelectedVisibleLayer()) return;

            Reset();

            mouseDownPosition = e.mousePosition;
            isDragging = true;

            e.Use();
        }

        private void Reset()
        {
            isDragging = false;
            selectionBoxRect = new Rect(0f, 0f, 0f, 0f);
            RemoveObjectHighlights();
        }

        public override void OnMouseUp(Event e)
        {
            if (e.button != 0) return;

            isDragging = false;
            selectionBoxRect = new Rect(0f, 0f, 0f, 0f);
        }

        private void HighlightObject(GameObject gameObject)
        {
            if (selections.Any(s => s.gameObject == gameObject)) return;
            if (gameObject.transform.root != tilemap.transform) return;

            var selection = new Selection
            {
                gameObject = gameObject
            };

            foreach (var renderer in gameObject.GetComponentsInChildren<Renderer>())
            {
                selection.renderers.Add(renderer);
                selection.materials.Add(renderer.sharedMaterial);
                renderer.material = selectMaterial;
            }

            selections.Add(selection);
        }

        private void RemoveObjectHighlights()
        {
            for (int i = 0; i < selections.Count; ++i)
            {
                RemoveHighlight(selections[i]);
            }

            selections.Clear();
        }

        private void RemoveHighlight(Selection selection)
        {
            for (int i = 0; i < selection.renderers.Count; i++)
            {
                selection.renderers[i].material = selection.materials[i];
            }
        }

        public override void OnMouseDrag(Event e)
        {
            if (!isDragging) return;
            if (e.button != 0) return;

            var min = Vector2.Min(mouseDownPosition, e.mousePosition);
            var max = Vector2.Max(mouseDownPosition, e.mousePosition);
            selectionBoxRect = Rect.MinMaxRect(
                min.x,
                min.y,
                max.x,
                max.y);

            RemoveObjectHighlights();

            foreach (var gameObject in HandleUtility.PickRectObjects(selectionBoxRect, true))
            {
                HighlightObject(gameObject);
            }
        }

        public override void OnRender(Event e)
        {
            if (isDragging)
            {
                RenderSelectionBox();
            }
        }

        public override void OnDestroy()
        {
            RemoveObjectHighlights();
        }

        public override void OnKeyDown(Event e)
        {
            if (e.keyCode == KeyCode.Delete && selections.Count > 0)
            {
                foreach (var selection in selections)
                {
                    RemoveHighlight(selection);
                    Undo.DestroyObjectImmediate(selection.gameObject);
                }

                selections.Clear();

                e.Use();
            }
        }

        private void RenderSelectionBox()
        {
            Handles.BeginGUI();

            GUI.backgroundColor = new Color(1f, 1f, 1f, 0.35f);
            GUI.Box(selectionBoxRect, "", boxStyle);

            Handles.EndGUI();
        }
    }
}

#endif