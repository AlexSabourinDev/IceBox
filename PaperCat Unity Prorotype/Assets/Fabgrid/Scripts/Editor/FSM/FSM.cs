namespace Fabgrid
{
    public class FSM
    {
        private State currentState;

        public void Transition(State state)
        {
            currentState?.OnExit();
            currentState = state;
            currentState.OnEnter();
        }

        /// <summary>
        /// Call this method from your OnSceneGUI method.
        /// </summary>
        public void OnSceneGUI()
        {
            currentState?.OnSceneGUI();
        }

        /// <summary>
        /// Call this method from your OnInspectorGUI method.
        /// </summary>
        public void OnInspectorGUI()
        {
            currentState?.OnSceneGUI();
        }

        public void OnDestroy()
        {
            currentState?.OnExit();
        }
    }
}