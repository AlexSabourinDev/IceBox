#if UNITY_EDITOR

using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.Assertions;

namespace Fabgrid
{
    public class EditTilePopup : EditorWindow
    {
        private const float spaceBetweenFields = 8;

        public Tile Tile { get; set; }
        public Tilemap3D Tilemap { get; set; }

        private void OnGUI()
        {
            if(Tile == null || Tilemap == null)
                return;

            Tile.size = EditorGUILayout.Vector3Field("Size", Tile.size);
            GUILayout.Space(spaceBetweenFields);
            Tile.offset = EditorGUILayout.Vector3Field("Offset", Tile.offset);
            GUILayout.Space(spaceBetweenFields);

            var options = Tilemap.categories.Select(category => category.name);

            EditorGUI.BeginChangeCheck();
            int categoryIndex = EditorGUILayout.Popup(new GUIContent("Category"), GetCategoryIndex(), options.ToArray());
            if (EditorGUI.EndChangeCheck())
            {
                var category = Tilemap.categories.ElementAtOrDefault(categoryIndex);
                Assert.IsNotNull(category);

                if (category == Tile.category)
                {
                    Tile.category = null;
                }
                else
                {
                    Tile.category = category;
                }

                Tilemap.Refresh();
            }
        }

        private int GetCategoryIndex()
        {
            if (Tile.category == null || Tile.category.IsEmpty())
                return -1;

            for (int i = 0; i < Tilemap.categories.Count; ++i)
            {
                if (Tilemap.categories[i].name == Tile.category.name)
                    return i;
            }

            return -1;
        }
    }
}

#endif