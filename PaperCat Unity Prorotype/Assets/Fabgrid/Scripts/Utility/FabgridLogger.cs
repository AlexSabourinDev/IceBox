using System.Collections;
using UnityEngine;

namespace Fabgrid
{
    public static class FabgridLogger
    {
        public static void LogError(string message)
        {
            Debug.LogError($"Fabgrid Error: {message}");
        }

        public static void LogWarning(string message)
        {
            Debug.LogWarning($"Fabgrid Warning: {message}");
        }

        public static void LogInfo(string message)
        {
            Debug.Log($"Fabgrid Info: {message}");
        }
    }
}