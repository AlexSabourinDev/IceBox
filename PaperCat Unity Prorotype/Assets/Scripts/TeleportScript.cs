using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class TeleportScript : MonoBehaviour
{

    public Transform target;
    public string toTeleportTag = "Avatar";
    

    private void OnTriggerEnter(Collider other)
    {
        if (other.tag == "Avatar")
        {
            other.transform.position = target.position;
            other.SendMessage("Teleport", SendMessageOptions.DontRequireReceiver);
        }
    }
}
