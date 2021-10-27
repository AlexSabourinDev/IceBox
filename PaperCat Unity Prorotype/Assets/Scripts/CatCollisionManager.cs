using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CatCollisionManager : MonoBehaviour
{
    CatMotor cm;

    void Awake()
    {
        cm = GetComponent<CatMotor>();
    }
    
    private void OnTriggerEnter(Collider other)
    {
        if (other.tag == "TriggerZone")
        {
            other.SendMessage("Trigger");
        }

        if (other.tag == "Checkpoint")
        {
            cm.UpdateCheckpoint(other.transform);   
            other.SendMessage("Trigger");
        }
        
        if (other.tag == "Deadly")
        {
            cm.RestartFromCheckpoint();
        }


    }
}
