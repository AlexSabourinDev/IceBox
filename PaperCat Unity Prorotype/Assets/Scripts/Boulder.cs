using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.ProBuilder;

public enum BoulderDirections
{
    South,
    East,
    West,
    North
}

[RequireComponent(typeof(ResettableObject))]
public class Boulder : MonoBehaviour
{
    public BoulderDirections direction = BoulderDirections.South;

    public float initialVelocity = 2f;
    public float force = 2f;
    public bool rolling;
    

    Rigidbody rb;
    Transform t;
    ProBuilderMesh pbm;
    WheelCollider wheel;
    MeshCollider cylander;
    BoulderKillzoneFollow kzf;
    

    float groundDetectionRayLength = 0.85f;
    float rearGroundDetectionOffset = 0.8f;

    bool moving;


    int GroundMask
    {
        get
        {
            return LayerMask.GetMask(new string[1] { "Ground" });
        }
    }

    Vector3 Forward
    {
        get
        {
            switch (direction)
            {
                case BoulderDirections.South:
                    return Vector3.back;
                case BoulderDirections.East:
                    return Vector3.right;
                case BoulderDirections.West:
                    return Vector3.left;
                case BoulderDirections.North:
                    return Vector3.forward;
                default:
                    Debug.LogWarning("direction variable is not assigned, returning zero for boulder direction.");
                    return Vector3.zero;
            }
        }
    }

    void Setup()
    {
        rb = GetComponent<Rigidbody>();
        t = transform;
        pbm = GetComponentInChildren<ProBuilderMesh>();
        cylander = GetComponentInChildren<MeshCollider>();
        //wheel = GetComponent<WheelCollider>();
        kzf = GetComponentInChildren<BoulderKillzoneFollow>();

        UpdateFacing(Forward);
        
    }

    void FixedUpdate()
    {
        if (!rolling) return;
        if (!moving)
        {
            rb.AddForce(Forward * initialVelocity, ForceMode.VelocityChange);
            moving = true;
        }
        else
        {
            rb.AddForce(Forward * force, ForceMode.Acceleration);

        }

        if (transform.position.y < -50)
        {
            Destroy(this.gameObject);
        }
    }

    void UpdateFacing(Vector3 newForward)
    {
        Quaternion newRotation = Quaternion.LookRotation(Forward, Vector3.up);
        t.SetPositionAndRotation(t.position, newRotation);
        if (kzf != null)
        {
            kzf.SetFacing(newRotation);
        }
        else
        {
            kzf = GetComponentInChildren<BoulderKillzoneFollow>();
            kzf.SetFacing(newRotation);
        }
    }

    bool IsGrounded()
    {
        if (rb.velocity.y <= 0)
        {
            //Debug.Log("Grounded.");
            Vector3 start1 = transform.position;
            Vector3 end1 = start1 + Vector3.down * groundDetectionRayLength;
            Vector3 start2 = transform.position - Forward * rearGroundDetectionOffset;
            Vector3 end2 = start2 + Vector3.down * groundDetectionRayLength;

            RaycastHit rayInfo1;
            RaycastHit rayInfo2;

            bool hitFront = Physics.Linecast(start1, end1, out rayInfo1, GroundMask);
            bool hitBack = Physics.Linecast(start2, end2, out rayInfo2, GroundMask);

            if (hitFront || hitBack)
            {
                return true;
            }
            else
            {
                return false;
            }

        }
        else
        {
            return false;
        }
    }

    private void OnDrawGizmos()
    {
        Vector3 start1 = transform.position;
        Vector3 end1 = start1 + Vector3.down * groundDetectionRayLength;
        Vector3 start2 = transform.position - Forward * rearGroundDetectionOffset;
        Vector3 end2 = start2 + Vector3.down * groundDetectionRayLength;

        Gizmos.color = Color.magenta;
        Gizmos.DrawLine(start1, end1);
        Gizmos.DrawLine(start2, end2);
    }

    private void Kill()
    {
        ResettableObject reset = GetComponent<ResettableObject>();
        reset.enabled = false;
        gameObject.SetActive(false);
    }

    private void OnEnable()
    {
        Setup();

    }

    private void OnDisable()
    {
        rb.velocity = Vector3.zero;
        moving = false;
        CancelInvoke();
    }
}
