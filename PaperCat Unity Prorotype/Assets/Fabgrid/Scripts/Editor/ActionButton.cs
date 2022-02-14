using UnityEngine.UIElements;
using System;

namespace Fabgrid
{
    public class ActionButton : IDisposable
    {
        private Button button;
        private Action<EventBase> onClick;

        public ActionButton(VisualElement root, string name, Action<EventBase> onClick)
        {
            this.button = root.Q<Button>(name);
            this.onClick = onClick;

            button.clickable.clickedWithEventInfo += onClick;
        }

        public void Invoke(EventBase e)
        {
            onClick.Invoke(e);
        }

        public void Dispose()
        {
            button.clickable.clickedWithEventInfo -= onClick;
        }
    }
}
