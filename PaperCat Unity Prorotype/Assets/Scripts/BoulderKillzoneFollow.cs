using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class BoulderKillzoneFollow : MonoBehaviour
{
    Transform daddy;
    Quaternion initialRotation;
    Transform t;
    Vector3 offset;

    void OnEnable()
    {
        t = transform;
        daddy = t.parent;
        
        //initialRotation = t.rotation;
    }

    void Update()
    {
        if (daddy == null) return;
        FixPosition();
        
    }

    public void SetFacing(Quaternion rotation)
    {
        initialRotation = rotation;
        //FixPosition();
    }

    void FixPosition()
    {
        t.position = daddy.position + Vector3.one * 0.001f;
        t.rotation = initialRotation;
    }
}
