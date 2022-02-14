#if UNITY_EDITOR

using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public static class DrawingUtility
    {
        public static void DrawGrid(Tilemap3D tilemap)
        {
            Handles.color = tilemap.gridColor;
            Handles.zTest = UnityEngine.Rendering.CompareFunction.Less;

            var verticalOffset = Vector3.up * tilemap.floorIndex * tilemap.floorSpacing;
            var gridCenterToTilemap = (tilemap.transform.position + (new Vector3(1f, 0f, 1f) * tilemap.gridSpacing * tilemap.gridSize * 0.5f))
                - tilemap.transform.position;

            var lineLength = tilemap.gridSize * tilemap.gridSpacing;

            for (int lineIndex = 0; lineIndex <= tilemap.gridSize; ++lineIndex)
            {
                var sideOffsetAmount = lineIndex * tilemap.gridSpacing;

                var start = tilemap.transform.position + (Vector3.right * sideOffsetAmount);
                var end = tilemap.transform.position + (Vector3.forward * lineLength) + (Vector3.right * sideOffsetAmount);

                Handles.DrawLine(
                    start + verticalOffset - gridCenterToTilemap,
                    end + verticalOffset - gridCenterToTilemap);

                start = tilemap.transform.position + (Vector3.forward * sideOffsetAmount);
                end = tilemap.transform.position + (Vector3.right * lineLength) + (Vector3.forward * sideOffsetAmount);

                Handles.DrawLine(start + verticalOffset - gridCenterToTilemap,
                    end + verticalOffset - gridCenterToTilemap);
            }
        }
    }
}

#endif