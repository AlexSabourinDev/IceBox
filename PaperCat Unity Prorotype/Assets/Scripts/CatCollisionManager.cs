using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CatCollisionManager : MonoBehaviour
{
    CatMotor cm;

    // Start is called before the first frame update
    void Awake()
    {
        cm = GetComponent<CatMotor>();
    }

    // Update is called once per frame
    void Update()
    {
        
    }

    private void OnTriggerEnter(Collider other)
    {
        if (other.tag == "TriggerZone")
        {
            ActivateTriggerZone triggerZone = other.GetComponent<ActivateTriggerZone>();
            triggerZone.Trigger();
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
