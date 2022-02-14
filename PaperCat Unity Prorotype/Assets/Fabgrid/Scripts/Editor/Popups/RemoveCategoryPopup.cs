#if UNITY_EDITOR

using UnityEngine;
using UnityEditor;

namespace Fabgrid
{
    public class RemoveCategoryPopup : EditorWindow
    {
        public Tilemap3D Tilemap { get; set; }

        private void OnGUI()
        {
            if (Tilemap == null)
                return;

            EditorGUILayout.LabelField("Categories");
            GUILayout.Space(10);

            Category categoryToRemove = null;

            foreach (var category in Tilemap.categories)
            {
                GUILayout.BeginHorizontal();

                EditorGUILayout.LabelField(category.name);
                if (GUILayout.Button("Remove"))
                {
                    categoryToRemove = category;
                }

                GUILayout.EndHorizontal();
            }

            if (categoryToRemove != null)
            {
                RemoveCategory(categoryToRemove);
            }
        }

        private void RemoveCategory(Category categoryToRemove)
        {
            bool userWantsToRemove = EditorUtility.DisplayDialog("Remove category", $"Are you sure you want to remove the category '{categoryToRemove.name}'?", "Yes", "No");

            if (!userWantsToRemove)
                return;

            foreach (var tile in Tilemap.tiles)
            {
                if (tile.category == categoryToRemove)
                    tile.category = null;
            }

            Tilemap.categories.Remove(categoryToRemove);
            Tilemap.Refresh();
        }
    }
}

#endif