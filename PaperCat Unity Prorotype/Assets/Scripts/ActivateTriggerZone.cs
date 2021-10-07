using System.Collections;
using System.Collections.Generic;
using UnityEngine;


public class ActivateTriggerZone : MonoBehaviour
{
    public GameObject[] toActivate;
    

    void OnEnable()
    {
        for (int i = 0; i < toActivate.Length; i++)
        {
            toActivate[i].SetActive(false);
        }
    }

    void Update()
    {
        
    }

    public void Trigger()
    {
        for (int i = 0; i < toActivate.Length; i++)
        {
            toActivate[i].SetActive(true);
        }
    }
}
