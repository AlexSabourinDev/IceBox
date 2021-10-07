using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CheckpointManager : MonoBehaviour
{
    Checkpoint[] chilluns;
    //int activeCheckpoint = -1;

    public static CheckpointManager Instance;

    void Awake()
    {
        chilluns = GetComponentsInChildren<Checkpoint>();
        if (Instance != null)
        {
            Debug.Log("uh, there's already a checkpoint manager...? I'm going to take its place though. " +
                "Info: " + Instance.name + ", " + Instance.transform.position);
        }
        Instance = this;
    }

    void Update()
    {
        
    }

    public void DeactivateOtherCheckpoints (Checkpoint activeCheckpoint)
    {
        for (int i = 0; i < chilluns.Length; i++)
        {
            if (chilluns[i] != activeCheckpoint)
            {
                chilluns[i].Deactivate();
            }
        }
    }
}
