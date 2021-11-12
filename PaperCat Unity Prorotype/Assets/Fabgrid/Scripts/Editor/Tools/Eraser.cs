using UnityEditor;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Fabgrid
{
    public class Eraser : Tool
    {
        private Mesh eraserMesh;
        private Material eraserMaterial;

        public Eraser(Tilemap3D tilemap) : base(tilemap)
        {
            toolType = ToolType.Eraser;
            LoadResources();
        }

        private void LoadResources()
        {
            var fabgridPath = PathUtility.GetFabgridFolder();
            eraserMesh = AssetDatabase.LoadAssetAtPath<GameObject>($"{fabgridPath}/Prefabs/Sphere.prefab").
                GetComponentsInChildren<MeshFilter>(true)[0].sharedMesh;
            eraserMaterial = AssetDatabase.LoadAssetAtPath<Material>($"{fabgridPath}/Materials/FabgridEraserMaterial.mat");
        }

        public override void OnRender(Event e)
        {
            var position = EraserPositionCalculator.GetPosition(e.mousePosition, tilemap);
            var mesh = eraserMesh;
            var material = eraserMaterial;

            material.SetPass(0);
            Graphics.DrawMeshNow(mesh,
                position,
                tilemap.tileRotation);
        }

        public override void OnMouseDown(Event e)
        {
            if (e.button != 0) return;
            if (e.alt || Keyboard.current.leftAltKey.isPressed) return;
            if (e.control || Keyboard.current.leftCtrlKey.isPressed) return;

            RemoveSelectedGameObject();
            e.Use();
        }

        public void RemoveSelectedGameObject()
        {
            if (tilemap.selectedGameObject != null)
            {
                if (CanRemove(tilemap.selectedGameObject))
                {
                    var rootTransform = GetRootTransform(tilemap.selectedGameObject);
                    Undo.DestroyObjectImmediate(rootTransform.gameObject);
                }
            }
        }

        private bool CanRemove(GameObject gameObject)
        {
            return gameObject.transform.parent.root.Equals(tilemap.transform);
        }

        private Transform GetRootTransform(GameObject gameObject)
        {
            Transform transform = gameObject.transform;

            while (transform != tilemap.transform)
            {
                if (transform.parent == tilemap.transform) break;

                transform = transform.parent;
            }

            return transform;
        }
    }
}