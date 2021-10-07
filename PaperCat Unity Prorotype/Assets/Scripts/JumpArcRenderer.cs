using System.Collections;
using System.Collections.Generic;
using UnityEngine;

[RequireComponent(typeof(LineRenderer))]
public class JumpArcRenderer : MonoBehaviour
{
    bool active;
    public bool Active
    {
        get
        {
            return active;
        }
        set
        {
            active = value;
            lr.enabled = value;
        }
    }

    float step = 0.1f;
    float arcLength = 5f;
    LineRenderer lr;
    void Start()
    {
        lr = GetComponent<LineRenderer>();
    }

    void Update()
    {

    }

    public void UpdatePointsAlongJumpArc(Vector3 initPos, Vector3 initVel, Vector3 impulseVel, float mass = 1f)
    {
        int stepCount = (int) Mathf.Round(arcLength / step);
        lr.positionCount = stepCount;
        Active = true;
        lr.SetPosition(0, initPos);
        Vector3 gravity = -Physics.gravity;
        Vector3 totalVelocity = (initVel + impulseVel) / mass;

        for (int i = 0; i < stepCount; i ++)
        {
            float t = i * step;
            Vector3 nextPosition = initPos + totalVelocity * t - 0.5f * gravity * t * t; // in lieu of step^2
            
            lr.SetPosition(i, nextPosition);
        }
    }

    public void StopArc()
    {
        Active = false;
    }
}
