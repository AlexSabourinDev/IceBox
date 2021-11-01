using UnityEngine.UIElements;

namespace Fabgrid
{
    public static class VisualElementExtensions
    {
        public static void Hide(this VisualElement visualElement)
        {
            visualElement.SetEnabled(false);
            visualElement.style.display = DisplayStyle.None;
        }

        public static void Show(this VisualElement visualElement)
        {
            visualElement.SetEnabled(true);
            visualElement.style.display = DisplayStyle.Flex;
        }
    }
}