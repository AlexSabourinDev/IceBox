using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Help : State
    {
        public Help(VisualElement root) : base(root)
        {}

        public override void OnEnter()
        {
            var mainPanelHeader = root.Q<Label>("main-panel-header");
            mainPanelHeader.text = "Help";
        }
    }
}