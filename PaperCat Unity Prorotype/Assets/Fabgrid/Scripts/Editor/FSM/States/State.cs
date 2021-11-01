using UnityEngine.UIElements;

namespace Fabgrid
{
    public abstract class State
    {
        protected VisualElement root;

        protected State(VisualElement root)
        {
            this.root = root;
        }

        public virtual void OnEnter()
        {
        }

        public virtual void OnInspectorGUI()
        {
        }

        public virtual void OnSceneGUI()
        {
        }

        public virtual void OnExit()
        {
        }
    }
}