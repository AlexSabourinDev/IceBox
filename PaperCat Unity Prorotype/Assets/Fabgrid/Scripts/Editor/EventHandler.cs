using System;
using UnityEngine;

namespace Fabgrid
{
    public class EventHandler
    {
        public Action<Event> OnSceneGUI;
        public Action<Event> OnMouseMove;
        public Action<Event> OnMouseDrag;
        public Action<Event> OnMouseUp;
        public Action<Event> OnMouseDown;
        public Action<Event> OnKeyDown;
        public Action<Event> OnKeyUp;

        public void ProcessEvents(Event e)
        {
            OnSceneGUI?.Invoke(e);

            switch (e.type)
            {
                case EventType.MouseDrag:
                    OnMouseDrag?.Invoke(e);
                    break;

                case EventType.MouseMove:
                    OnMouseMove?.Invoke(e);
                    break;

                case EventType.MouseDown:
                    OnMouseDown?.Invoke(e);
                    break;

                case EventType.MouseUp:
                    OnMouseUp?.Invoke(e);
                    break;

                case EventType.KeyDown:
                    OnKeyDown?.Invoke(e);
                    break;

                case EventType.KeyUp:
                    OnKeyUp?.Invoke(e);
                    break;
            }
        }
    }
}