using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class DollyTrackFollowYAxis : MonoBehaviour
{
    Transform followTarget;
    CatMotor cm;
    public float lerpAmount = 0.8f;

    float groundY;
    float offset;
    Vector3 initialPosition;

    bool initialized;

    private void Start()
    {
        cm = CatMotor.Instance;
        followTarget = cm.transform;
    }

    void Init()
    {
        initialized = true;

        groundY = followTarget.position.y;
        offset = transform.position.y - groundY;
    }

    // Update is called once per frame
    void FixedUpdate()
    {
        if (cm.Grounded)
        {
            if (!initialized)
            {
                Init();
            }
            groundY = followTarget.position.y;
        }
        if (!initialized)
            return;

        Vector3 pos = transform.position;
        transform.position = Vector3.Lerp(pos, new Vector3(pos.x, groundY + offset, pos.z), lerpAmount);
    }
}
