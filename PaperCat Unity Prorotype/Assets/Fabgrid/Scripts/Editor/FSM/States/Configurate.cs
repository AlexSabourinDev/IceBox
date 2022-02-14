#if UNITY_EDITOR

using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Configurate : State
    {
        private readonly SerializedObject serializedTilemap;

        public Configurate(VisualElement root, Tilemap3D tilemap) : base(root)
        {
            this.serializedTilemap = new SerializedObject(tilemap);
        }

        public override void OnEnter()
        {
            var mainPanelHeader = root.Q<Label>("main-panel-header");
            mainPanelHeader.text = "Configuration";

            BindControls();

            var configurationPanel = root.Q("configuration-panel");
            //StylingUtility.AddClassToAllOfType<Label>(configurationPanel, "field-label");
        }

        private void BindControls()
        {
            var gridColorField = root.Q<ColorField>("grid-color-field");
            var gridColorProperty = serializedTilemap.FindProperty("gridColor");
            gridColorField.BindProperty(gridColorProperty);

            var rotationStepAngleFloatField = root.Q<FloatField>("rotation-step-angle-float-field");
            var rotationStepAngleProperty = serializedTilemap.FindProperty("rotationStepAngle");
            rotationStepAngleFloatField.BindProperty(rotationStepAngleProperty);

            var gridSpacingFloatField = root.Q<FloatField>("grid-spacing-float-field");
            var gridSpacingProperty = serializedTilemap.FindProperty("gridSpacing");
            gridSpacingFloatField.BindProperty(gridSpacingProperty);

            var floorSpacingField = root.Q<FloatField>("floor-spacing-field");
            floorSpacingField.Bind(serializedTilemap);

            var gridSizeField = root.Q<IntegerField>("grid-size-field");
            var gridSizeProperty = serializedTilemap.FindProperty("gridSize");
            gridSizeField.BindProperty(gridSizeProperty);

            var snapOptionField = root.Q<EnumField>("snap-option-field");
            snapOptionField.Bind(serializedTilemap);

            BindKeyBindingControls();
        }

        private void BindKeyBindingControls()
        {
            var selectionBoxKey = root.Q<EnumField>("selection-box-key-field");
            var brushKey = root.Q<EnumField>("brush-key-field");
            var eraserKey = root.Q<EnumField>("eraser-key-field");
            var rectangleToolKey = root.Q<EnumField>("rectangle-tool-key-field");
            var raiseFloorKey = root.Q<EnumField>("raise-floor-key-field");
            var lowerFloorKey = root.Q<EnumField>("lower-floor-key-field");
            var rotateTileAroundYKey = root.Q<EnumField>("rotate-tile-around-y-key-field");
            var rotateTileAroundZKey = root.Q<EnumField>("rotate-tile-around-z-key-field");
            var resetTileRotationKey = root.Q<EnumField>("reset-tile-rotation-key-field");

            selectionBoxKey.Bind(serializedTilemap);
            brushKey.Bind(serializedTilemap);
            eraserKey.Bind(serializedTilemap);
            rectangleToolKey.Bind(serializedTilemap);
            raiseFloorKey.Bind(serializedTilemap);
            lowerFloorKey.Bind(serializedTilemap);
            rotateTileAroundZKey.Bind(serializedTilemap);
            rotateTileAroundYKey.Bind(serializedTilemap);
            resetTileRotationKey.Bind(serializedTilemap);
        }
    }
}

#endif