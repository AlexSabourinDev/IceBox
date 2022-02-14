namespace Fabgrid
{
    public class Panel
    {
        public Panel(string name, string stylesheetPath, string visualTreeAssetPath, State state, string buttonIconPath)
        {
            Name = name;
            StylesheetPath = stylesheetPath;
            VisualTreeAssetPath = visualTreeAssetPath;
            State = state;
            ButtonIconPath = buttonIconPath;
        }

        public string Name { get; private set; }
        public string StylesheetPath { get; private set; }
        public string VisualTreeAssetPath { get; private set; }
        public State State { get; private set; }
        public string ButtonIconPath { get; private set; }
    }
}
