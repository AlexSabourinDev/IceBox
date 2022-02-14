using System.Collections;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Fabgrid
{
    public static class BatchingUtility
    {
        /// <summary>
        /// Batches together game objects that have the same
        /// set of shared materials.
        /// Returns an array of batches.
        /// </summary>
        public static GameObject BatchLayer(GameObject layer)
        {
            Dictionary<string, List<GameObject>> batches = new Dictionary<string, List<GameObject>>();

            for(int i = 0; i < layer.transform.childCount; ++i)
            {
                var transform = layer.transform.GetChild(i);

                if (transform.GetComponent<LayerBehaviour>())
                    continue;

                if (string.IsNullOrEmpty(transform.name))
                    continue;

                if(!batches.ContainsKey(transform.name))
                    batches.Add(transform.name, new List<GameObject>());

                batches[transform.name].Add(GameObject.Instantiate(transform.gameObject));
            }

            var layerParent = new GameObject(layer.name);
            layerParent.transform.position = layer.transform.position;
            layerParent.transform.rotation = layer.transform.rotation;
            layerParent.transform.localScale = layer.transform.localScale;

            // Combine each batch
            foreach (var id in batches.Keys)
            {
                var gameObjects = batches[id];

                var root = new GameObject($"{id} batch");
                root.transform.position = layer.transform.position;
                root.transform.rotation = layer.transform.rotation;
                root.transform.localScale = layer.transform.localScale;

                foreach (var go in gameObjects)
                    go.transform.SetParent(root.transform, true);

                StaticBatchingUtility.Combine(gameObjects.ToArray(), root);

                root.transform.SetParent(layerParent.transform, true);
            }

            return layerParent;
        }
    }
}