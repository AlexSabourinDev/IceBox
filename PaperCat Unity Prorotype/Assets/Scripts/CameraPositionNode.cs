using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CameraPositionNode : MonoBehaviour
{
    CatMotor cm;
    Transform avatarT;
    float xPos;
    float yInit;
    float yOffset;
    float zOffset;
    float catLastKnownY = Mathf.Infinity;
    float lerpAmount = 0.1f;

    void Start()
    {
        cm = CatMotor.Instance;
        avatarT = cm.transform;
        xPos = transform.position.x;
        yInit = transform.position.y;
        zOffset = transform.position.z - avatarT.position.z;
        transform.parent = null;
    }

    void FixedUpdate()
    {
        if (catLastKnownY < Mathf.Infinity)
        {
            Vector3 wouldbePosition = new Vector3(xPos, catLastKnownY + yOffset, avatarT.position.z + zOffset);
            transform.position = Vector3.Lerp(transform.position, wouldbePosition, lerpAmount);
        }

        if (cm.Grounded)
        {
            if (catLastKnownY >= Mathf.Infinity)
            {
                yOffset = yInit - avatarT.position.y;
                Debug.Log("Catching cat position");
            }
            catLastKnownY = avatarT.position.y;
        }
    }
}
