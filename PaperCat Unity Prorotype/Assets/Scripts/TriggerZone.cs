using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class TriggerZone : MonoBehaviour
{
    public GameObject[] toActivate;


    public void Trigger()
    {
        for (int i = 0; i < toActivate.Length; i++)
        {
            toActivate[i].SendMessage("Trigger");
        }
    }
}
