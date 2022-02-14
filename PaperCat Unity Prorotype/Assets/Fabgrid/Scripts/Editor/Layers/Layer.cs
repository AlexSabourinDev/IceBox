using System;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public class Layer : IDisposable
    {
        public VisualElement element;
        public ActionButton deleteButton;
        public GameObject gameObject;

        public void Dispose()
        {
            deleteButton.Dispose();
        }
    }
}
