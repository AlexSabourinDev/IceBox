namespace Fabgrid
{
    public class FSM
    {
        public State CurrentState { get; private set; }

        public void Transition(State state)
        {
            CurrentState?.OnExit();
            CurrentState = state;
            CurrentState.OnEnter();
        }

        /// <summary>
        /// Call this method from your OnSceneGUI method.
        /// </summary>
        public void OnSceneGUI()
        {
            CurrentState?.OnSceneGUI();
        }

        /// <summary>
        /// Call this method from your OnInspectorGUI method.
        /// </summary>
        public void OnInspectorGUI()
        {
            CurrentState?.OnSceneGUI();
        }

        public void OnDestroy()
        {
            CurrentState?.OnExit();
        }
    }
}