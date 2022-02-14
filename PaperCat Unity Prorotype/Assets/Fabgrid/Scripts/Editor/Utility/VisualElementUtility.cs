#if UNITY_EDITOR

using System.Collections;
using System.Collections.Generic;
using System.Linq;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;

namespace Fabgrid
{
    public static class VisualElementUtility
    {
        public static PopupField<string> CreateTilePaletteField(string label, EventCallback<ChangeEvent<string>> callback, VisualElement parent)
        {
            var tilePalettes = Resources.FindObjectsOfTypeAll<TileSet>().ToList();
            var tilePaletteStrings = new List<string>();
            tilePalettes.ForEach(tp => tilePaletteStrings.Add(tp.name));

            if (tilePaletteStrings.Count <= 0)
            {
                tilePaletteStrings.Add("<no tile palettes created>");
            }

            var tilePaletteField = new PopupField<string>(
                                   label,
                                   tilePaletteStrings,
                                   tilePaletteStrings.First());

            parent.Add(tilePaletteField);
            tilePaletteField.RegisterValueChangedCallback(callback);

            return tilePaletteField;
        }
    }
}

#endif