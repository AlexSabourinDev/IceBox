using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class AnimationPlayer : MonoBehaviour
{

    
    public void Trigger()
    {
        this.GetComponent<Animator>().SetTrigger("Activated");
    }
}
