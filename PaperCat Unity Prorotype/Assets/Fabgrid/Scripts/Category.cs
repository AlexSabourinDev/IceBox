using System;
using UnityEngine;

namespace Fabgrid
{
    [Serializable]
    public class Category
    {
        public string name = "";
        public Color color = Color.white;

        public bool IsEmpty()
        {
            return string.IsNullOrEmpty(name);
        }
    }
}