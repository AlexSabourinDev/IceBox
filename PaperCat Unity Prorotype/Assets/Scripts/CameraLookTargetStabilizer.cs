using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CameraLookTargetStabilizer : MonoBehaviour
{
    Transform cat;
    float yOffset;
    float zOffset;
    float lerpAmount = 0.1f;
    float xPosition;

    // Start is called before the first frame update
    void Start()
    {
        cat = CatMotor.Instance.transform;
        transform.parent = null;
        Vector3 avatarPositionDifferential = transform.position - CatMotor.Instance.transform.position;
        zOffset = avatarPositionDifferential.z;
        yOffset = avatarPositionDifferential.y;
        xPosition = transform.position.x;
    }

    // Update is called once per frame
    void FixedUpdate()
    {
        Vector3 wouldbePosition = new Vector3(xPosition, cat.position.y + yOffset, cat.position.z + zOffset);
        transform.position = Vector3.Lerp(transform.position, wouldbePosition, lerpAmount);
    }
}
