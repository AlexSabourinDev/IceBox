using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Checkpoint : MonoBehaviour
{

    ParticleSystem particle;

    void Awake()
    {
        particle = GetComponentInChildren<ParticleSystem>(true);
        if (particle == null)
        {
            Debug.LogWarning("No particle guy on my checkpoint!");
        }
        particle.gameObject.SetActive(false);
    }

    public void Trigger()
    {
        particle.gameObject.SetActive(true);
        CheckpointManager.Instance.DeactivateOtherCheckpoints(this);
    }

    public void Deactivate()
    {
        particle.gameObject.SetActive(false);
    }
}
