using System.Collections.Generic;
using UnityEngine;

namespace Fabgrid
{
    [System.Serializable]
    public class SerializableDictionary<KeyType, ValueType>
        : Dictionary<KeyType, ValueType>, ISerializationCallbackReceiver
    {
        [SerializeField] private readonly List<KeyType> keys = new List<KeyType>();
        [SerializeField] private readonly List<ValueType> values = new List<ValueType>();

        public void OnBeforeSerialize()
        {
            keys.Clear();
            values.Clear();

            foreach (var pair in this)
            {
                keys.Add(pair.Key);
                values.Add(pair.Value);
            }
        }

        public void OnAfterDeserialize()
        {
            this.Clear();

            if (keys.Count != values.Count)
            {
                throw new System.Exception($"SerializableDictionary Error: There are {keys.Count} keys and {values.Count} values in the dictionary. Make sure both the key and value is serializable.");
            }

            for (int i = 0; i < keys.Count; ++i)
            {
                this.Add(keys[i], values[i]);
            }
        }
    }
}