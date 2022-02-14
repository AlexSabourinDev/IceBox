using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Dialog
    {
        public VisualElement Element { get; private set; }

        public Dialog(VisualElement root)
        {
            Element = new VisualElement();

            Element.style.width = new StyleLength(new Length(25.0f, LengthUnit.Percent));
            Element.style.height = new StyleLength(new Length(25.0f, LengthUnit.Percent));

            root.Add(Element);
        }


    }
}
