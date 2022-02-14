#if UNITY_EDITOR

using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.Assertions;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class LayerPanel
    {
        private VisualElement root;
        private Tilemap3D tilemap;
        private Dictionary<Toggle, GameObject> layerToggleToGameObject = new Dictionary<Toggle, GameObject>();
        private Dictionary<TextField, GameObject> layerTextFieldToGameObject = new Dictionary<TextField, GameObject>();

        public List<Layer> Layers { get; private set; } = new List<Layer>();
        public ActionButton AddLayerButton { get; private set; }

        public LayerPanel(VisualElement root, Tilemap3D tilemap)
        {
            this.root = root;
            this.tilemap = tilemap;

            var layerGameObject = tilemap.layers.Any() ? tilemap.layers.First().gameObject : CreateLayer();
            PopulateLayerItems();
            SelectLayer(layerGameObject);

            AddLayerButton = new ActionButton(root, "add-layer-button", OnClickAddLayerButton);
        }

        public void Free()
        {
            AddLayerButton.Dispose();
        }

        public void PopulateLayerItems()
        {
            Layers.Clear();
            layerToggleToGameObject.Clear();
            layerTextFieldToGameObject.Clear();

            var layersScrollView = root.Q<ScrollView>("layers-panel-scroll-view");
            layersScrollView.Clear();

            var layersToRemove = new List<GameObject>();

            foreach (var layerGameObject in tilemap.layers)
            {
                if (layerGameObject == null)
                {
                    layersToRemove.Add(layerGameObject);
                    continue;
                }

                var layerVisualElement = CreateLayerVisualElement(layerGameObject);
                layersScrollView.Add(layerVisualElement);

                if (tilemap.selectedLayer == layerGameObject)
                    SelectLayer(tilemap.selectedLayer.gameObject);

                var layer = new Layer();
                layer.element = layerVisualElement;
                layer.deleteButton = new ActionButton(layerVisualElement, "delete-layer-button", e => { OnClickDeleteLayerButton(layerGameObject); });
                layer.gameObject = layerGameObject;
                Layers.Add(layer);
            }

            foreach (var layer in layersToRemove)
            {
                tilemap.layers.Remove(layer);
            }
        }

        private VisualElement CreateLayerVisualElement(GameObject gameObject)
        {
            var visibilityToggle = new Toggle("");
            visibilityToggle.value = gameObject.activeSelf;
            visibilityToggle.style.marginRight = 32;
            visibilityToggle.RegisterValueChangedCallback(e => OnToggleLayerVisibility(e));
            visibilityToggle.RegisterCallback<PointerDownEvent>(OnPointerDownVisibilityToggle, TrickleDown.TrickleDown);
            layerToggleToGameObject.Add(visibilityToggle, gameObject);

            var layerNameTextField = new TextField();
            layerNameTextField.name = "layer-name-field";
            layerNameTextField.style.flexGrow = 1.0f;
            layerNameTextField.value = gameObject.name;
            layerNameTextField.RegisterValueChangedCallback(e => OnRenameLayer(e));
            layerTextFieldToGameObject.Add(layerNameTextField, gameObject);

            var deleteButton = new Button();
            deleteButton.name = "delete-layer-button";
            deleteButton.style.width = 16;
            deleteButton.style.height = 16;
            deleteButton.style.marginLeft = 32;
            deleteButton.text = "X";

            var layer = new VisualElement();
            layer.style.marginTop = 2;
            layer.style.marginBottom = 2;
            layer.style.justifyContent = Justify.SpaceBetween;
            layer.style.flexDirection = FlexDirection.Row;

            layer.RegisterCallback<PointerEnterEvent>(_ => OnPointerEnterLayer(layer), TrickleDown.TrickleDown);
            layer.RegisterCallback<PointerLeaveEvent>(_ => OnPointerLeaveLayer(layer), TrickleDown.TrickleDown);
            layer.RegisterCallback<PointerDownEvent>(e => OnPointerDownLayer(layer, gameObject, e), TrickleDown.NoTrickleDown);

            layer.Add(visibilityToggle);
            layer.Add(layerNameTextField);
            layer.Add(deleteButton);

            return layer;
        }

        private void OnPointerDownVisibilityToggle(PointerDownEvent e)
        {
            e.StopPropagation();
            e.StopImmediatePropagation();
        }

        private void OnPointerEnterLayer(VisualElement layer)
        {
            layer.AddToClassList("layer-hovered");
        }

        private void OnPointerLeaveLayer(VisualElement layer)
        {
            layer.RemoveFromClassList("layer-hovered");
        }

        private void OnPointerDownLayer(VisualElement layer, GameObject layerGameObject, PointerDownEvent e)
        {
            // Deselect layer if it is already selected
            if(tilemap.selectedLayer == layerGameObject)
            {
                ClearSelectedLayer();
                return;
            }

            SelectLayer(layerGameObject);

            e.StopImmediatePropagation();
            e.StopPropagation();
        }

        private void SelectLayer(GameObject layerGameObject)
        {
            ClearSelectedLayer();

            var layer = Layers.Find(l => l.gameObject == layerGameObject);
            if (layer == null) return;

            layer.element.AddToClassList("layer-selected");
            tilemap.selectedLayer = layer.gameObject;
        }

        private void OnClickDeleteLayerButton(GameObject gameObject)
        {
            bool shouldDeleteLayer = true;

            if (gameObject.transform.childCount > 0)
                shouldDeleteLayer = EditorUtility.DisplayDialog("Remove layer", $"Are you sure you want to delete the layer '{gameObject.name}'?", "Yes", "No");

            if (!shouldDeleteLayer)
                return;

            tilemap.layers.Remove(gameObject);
            Undo.DestroyObjectImmediate(gameObject);

            var layerGameObject = Layers.Any() ? Layers.First().gameObject : CreateLayer();
            PopulateLayerItems();
            SelectLayer(layerGameObject);
        }

        private GameObject CreateLayer()
        {
            var layerGameObject = new GameObject($"Layer {tilemap.layers.Count + 1}");
            layerGameObject.transform.SetParent(tilemap.transform);
            layerGameObject.transform.localPosition = Vector3.zero;
            layerGameObject.transform.localRotation = Quaternion.identity;

            var layer = layerGameObject.AddComponent<LayerBehaviour>();
            layer.tilemap = tilemap;

            tilemap.layers.Add(layerGameObject);

            return layerGameObject;
        }

        private void OnRenameLayer(ChangeEvent<string> e)
        {
            var textField = e.target as TextField;
            Assert.IsNotNull(textField);

            var layer = layerTextFieldToGameObject[textField];
            Assert.IsNotNull(layer);

            layer.name = e.newValue;
        }

        private void OnToggleLayerVisibility(ChangeEvent<bool> e)
        {
            var toggle = e.target as Toggle;
            Assert.IsNotNull(toggle);

            var layer = layerToggleToGameObject[toggle];
            Assert.IsNotNull(layer);

            layer.SetActive(e.newValue);

            e.StopPropagation();
            e.StopImmediatePropagation();
        }

        public void ClearSelectedLayer()
        {
            foreach (var l in root.Q<ScrollView>("layers-panel-scroll-view").Children())
                l.RemoveFromClassList("layer-selected");

            tilemap.selectedLayer = null;
        }

        private void OnClickAddLayerButton(EventBase e)
        {
            var layerGameObject = CreateLayer();
            PopulateLayerItems();
            SelectLayer(layerGameObject);
        }
    }
}

#endif