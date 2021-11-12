using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Spawner : MonoBehaviour
{

    public GameObject toSpawn;
    public bool startActive = true;
    public bool timed = true;
    public float[] spawnSequence = new float[] { 1f };

    float spawnTimer = 0;
    float spawnTiming = 0;
    int spawnSequenceIndex = 0;

    void OnEnable()
    {
        if (toSpawn == null)
        {
            Debug.LogWarning("The spawner has no template...");
        }
        toSpawn.SetActive(false);

        if (!startActive)
        {
            gameObject.SetActive(false);
        }
    }

    void Update()
    {
        if (timed)
        {
            if (spawnTimer <= spawnTiming)
            {
                spawnTimer = spawnSequence[spawnSequenceIndex];
                spawnSequenceIndex++;
                if (spawnSequenceIndex >= spawnSequence.Length)
                {
                    spawnSequenceIndex = 0;
                }

                SpawnChild();
            }

            spawnTimer -= Time.deltaTime;
        }
    }

    public void Trigger()
    {
        if (!timed)
        {
            SpawnChild();
        }
    }

    void SpawnChild()
    {
        GameObject child = Instantiate(toSpawn, transform.position, toSpawn.transform.rotation);
        child.SetActive(true);
    }
}
