using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Batching : State
    {
        private Button batchButton;
        private Tilemap3D tilemap;

        public Batching(VisualElement root, Tilemap3D tilemap) : base(root)
        {
            this.tilemap = tilemap;

        }

        private void OnClickBatchButton()
        {
            foreach (var layer in tilemap.layers)
            {
                BatchingUtility.BatchLayer(layer);
                FabgridLogger.LogInfo($"Created a batch of {layer.name}");
            }
        }

        public override void OnEnter()
        {
            var mainPanelHeader = root.Q<Label>("main-panel-header");
            mainPanelHeader.text = "Batching";

            batchButton = root.Q<Button>("batch-button");
            batchButton.clickable.clicked += OnClickBatchButton;
        }

        public override void OnExit()
        {
            batchButton.clickable.clicked -= OnClickBatchButton;
        }
    }
}