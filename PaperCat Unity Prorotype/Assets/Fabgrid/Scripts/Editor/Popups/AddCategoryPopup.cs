#if UNITY_EDITOR

using System.Linq;
using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public class AddCategoryPopup : EditorWindow
    {
        const float spaceBetweenFields = 8;
        
        public Category Category { get; set; }
        public Tilemap3D Tilemap { get; set; }

        private void OnGUI()
        {
            if (Category == null || Tilemap == null)
                return;

            GUILayout.Space(10);
            Category.name = EditorGUILayout.TextField("Name", Category.name);
            GUILayout.Space(spaceBetweenFields);
            Category.color = EditorGUILayout.ColorField("Color", Category.color);
            GUILayout.Space(10);

            if (GUILayout.Button("Add"))
            {
                if (!CanCreateCategory(out string errorMessage))
                {
                    EditorUtility.DisplayDialog("Error", errorMessage, "OK");
                }
                else
                {
                    Tilemap.categories.Add(Category);
                    Close();
                }
            }
        }

        private bool CanCreateCategory(out string errorMessage)
        {
            if(string.IsNullOrEmpty(Category.name))
            {
                errorMessage = "Category name cannot be empty.";
                return false;
            }

            if (Tilemap.categories.Any(category => category.name == Category.name))
            {
                errorMessage = "Category cannot have the same name as another category.";
                return false;
            }

            errorMessage = "";
            return true;
        }
    }
}

#endif