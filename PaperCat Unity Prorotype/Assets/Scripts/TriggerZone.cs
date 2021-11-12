using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class TriggerZone : MonoBehaviour
{
    public GameObject[] toActivate;
    public bool fireOnce = true;

    bool fired = false;

    public void Trigger()
    {
        if (fired)
        {
            return;
        }
        for (int i = 0; i < toActivate.Length; i++)
        {
            toActivate[i].SendMessage("Trigger");
        }
        if (fireOnce)
        {
            fired = true;
        }
    }
}
