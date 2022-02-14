#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public static class PathUtility
    {
        /// <summary>
        /// Returns null on fail.
        /// </summary>
        public static string AbsoluteToRelative(string absolutePath)
        {
            if (absolutePath.StartsWith(Application.dataPath))
            {
                var relativePath = $"Assets{absolutePath.Substring(Application.dataPath.Length)}";
                return relativePath;
            }

            return null;
        }

        public static string GetPathToFolder(string relativePath, string folderName)
        {
            var subFolders = AssetDatabase.GetSubFolders(relativePath);

            foreach (var folder in subFolders)
            {
                if (folder.EndsWith(folderName))
                {
                    return folder;
                }
            }

            return null;
        }

        public static string GetFabgridFolder()
        {
            return GetPathToFolder("Assets", "Fabgrid");
        }

        public static string PanelsPath => $"{GetFabgridFolder()}/Scripts/Editor/Panels";
    }
}

#endif