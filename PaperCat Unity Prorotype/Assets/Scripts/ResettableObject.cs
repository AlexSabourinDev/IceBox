using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class ResettableObject : MonoBehaviour
{
    Vector3 initPos;
    Quaternion initRot;

    void Awake()
    {
        initPos = transform.position;
        initRot = transform.rotation;

    }

    void Update()
    {
        
    }

    void OnEnable()
    {
        transform.SetPositionAndRotation(initPos, initRot);
    }

    void OnDisable()
    {
        transform.SetPositionAndRotation(initPos, initRot);
    }

    public void Reset()
    {
        SendMessage("OnEnable");
        SendMessage("OnDisable");
    }
}
