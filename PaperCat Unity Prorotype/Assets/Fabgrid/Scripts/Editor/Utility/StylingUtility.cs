using UnityEngine.UIElements;

namespace Fabgrid
{
    public static class StylingUtility
    {
        /// <summary>
        /// Adds a class to all the elements of a specific type.
        /// The element is skipped if it already contains the class.
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="root"></param>
        /// <param name="className"></param>
        public static void AddClassToAllOfType<T>(VisualElement root, string className) where T : VisualElement
        {
            var elements = root.Query<T>();
            elements.ForEach(element =>
            {
                if (!element.ClassListContains(className))
                {
                    element.AddToClassList(className);
                }
            });
        }
    }
}