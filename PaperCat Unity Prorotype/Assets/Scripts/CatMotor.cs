using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

public class CatMotor : MonoBehaviour
{
    public static CatMotor Instance;
    InputManager im;
    BoxCollider box;
    Rigidbody rb;
    CatMovementParameters mp;

    Transform visibleMesh;
    JumpArcRenderer jumpArcRenderer;

    float stickInputDeadzone = 0.1f;
    bool grounded = false;
    bool canJump = false;
    bool running = false;
    bool galloping = false;
    bool climbing = false;
    bool trotting = false;
    bool windingUpJump = false;
    bool wallDetectedLastFrame = false;
    float movementTimer = 0f;
    float jumpExtensionTimer = 0;
    float jumpRetractionTimer = 0;
    float jumpTargetPercentage = 0;
    float footstepTiming;
    Vector3 forward;
    Vector3 initialPosition;

    Transform previousCheckpoint;

    float TurnaroundTime
    {
        get
        {
            return running ? mp.gallopTurnaroundTime : mp.trotTurnaroundTime;
        }
    }

    int GroundMask
    {
        get
        {
            return LayerMask.GetMask(new string[1] { "Ground" });
        }
    }

    public bool Grounded
    {
        get
        {
            return grounded;
        }
    }

    public bool MovingOnGround
    {
        get
        {
            return IsGrounded() && (trotting || galloping);
        }
    }

    public bool Falling
    {
        get {
            return !grounded && rb.velocity.y < 0;
        }
    }

    public bool WillClimb
    {
        get
        {
            return !grounded && rb.velocity.y <= mp.minimumYVelocityToStartClimbing;
        }
    }

    private void Awake()
    {
        if (Instance != null)
        {
            Destroy(Instance.gameObject);
            Debug.Log("There was an ImposterCat, I am the avatar now.");
        }
        Instance = this;
    }

    void Start()
    {
        im = GetComponent<InputManager>();
        box = GetComponentInChildren<BoxCollider>();
        rb = GetComponent<Rigidbody>();
        mp = GetComponent<CatMovementParameters>();
        visibleMesh = GetComponentInChildren<MeshRenderer>().transform;
        jumpArcRenderer = GetComponentInChildren<JumpArcRenderer>(true);
        forward = Vector3.forward;
        initialPosition = transform.position;

    }

    void FixedUpdate()
    {
        //are we running?

        bool runButtonDown = im.GetAnyLeftShoulder();
        running = runButtonDown;
        //handle horizontal movement

        Vector2 leftInput = im.GetLeftStickInput();

        movementTimer -= Time.deltaTime;
        bool leftStickActivated = leftInput.magnitude > stickInputDeadzone;
        Vector3 newForward;
        if (leftStickActivated)
        {
            newForward = new Vector3(leftInput.x, 0, leftInput.y).normalized;
        }
        else
        {
            newForward = forward;
        }

        if (movementTimer <= 0)
        {
            if (grounded && !windingUpJump && !climbing)
            {
                if (leftStickActivated)
                {
                    footstepTiming = mp.GetFootstepTime(galloping);
                    float forceY;
                    Vector2 horizontalForce;
                    float directionChangeAngle = Vector3.Angle(forward, newForward);

                    if (directionChangeAngle > mp.stopAndTurnMinimumAngle)
                    {
                        movementTimer = directionChangeAngle / 180 * TurnaroundTime;
                        footstepTiming = movementTimer;
                    }
                    else
                    {
                        if (running)
                        {
                            rb.velocity = rb.velocity.magnitude * newForward;
                            galloping = true;
                            trotting = false;

                            bool highSpeed = rb.velocity.magnitude > mp.gallopHighSpeedThreshold;
                            forceY = mp.gallopImpulseForceY;
                            horizontalForce = im.GetLeftStickInput() * mp.GetHorizontalForceMagnitude(running, highSpeed);
                            Vector3 forceVector = new Vector3(horizontalForce.x, forceY, horizontalForce.y);
                            rb.AddForce(forceVector, ForceMode.Impulse);
                            movementTimer = mp.gallopImpulseTiming;
                        }
                        else
                        {
                            rb.velocity = rb.velocity.magnitude * newForward;
                            galloping = false;
                            trotting = true;
                            horizontalForce = im.GetLeftStickInput() * mp.trotImpulseForceX;

                            Vector3 forceVector = new Vector3(horizontalForce.x, 0, horizontalForce.y);

                            rb.AddForce(forceVector, ForceMode.Impulse);
                            movementTimer = mp.trotImpulseTiming;
                        }
                        UpdateFacing(newForward);
                    }


                    //turn the model to face our direction

                }
                else 
                {
                    ApplyHorizontalFrictionForce(mp.stopFrictionFactor);
                    if (MovingOnGround && rb.velocity.magnitude < 0.01f)
                        EnterStandingState();
                }
            }

            //do our climbing leap
            if (climbing)
            {
                Vector3 direction = Vector3.up;
                float force = mp.GetClimbForce(running);
                rb.AddForce(direction * force, ForceMode.Impulse);

                movementTimer = mp.GetClimbMovementTiming(running);
                footstepTiming = mp.GetClimbFootstepTime(running);
            }

        }
        else
        {
            //footsteps have their own friction value
            if (movementTimer <= footstepTiming)
            {
                if (MovingOnGround && grounded)
                {
                    //apply footstep friction
                    ApplyHorizontalFrictionForce(mp.movingFrictionFactor);
                }
                else if (climbing)
                {
                    ApplyVerticalFrictionForce(mp.climbStopFrictionFactor);
                }
                UpdateFacing(newForward);

            }
            else if (MovingOnGround)
            {
                ApplyHorizontalFrictionForce(mp.dragOnGroundWhileMoving);
            }
        }

        //handle jumping
        
        bool jumpButton = im.GetSouthButton();
        
        if (jumpButton && !windingUpJump && grounded && canJump)
        {
            windingUpJump = true;
            jumpTargetPercentage = mp.jumpArcInitialPercentage;
        }
        
        if (windingUpJump)
        {
            //change angle of jump if we're using the control stick
            if (leftStickActivated)
            {

                float directionChangeAngle = Vector3.Angle(forward, newForward);
                Vector3 newFacing = Vector3.RotateTowards(forward, newForward, mp.jumpArcRotationRadiansPerSecond * Time.deltaTime, 0);
                UpdateFacing(newFacing);

                if (directionChangeAngle < 45f)
                {
                    jumpRetractionTimer = 0;
                    jumpExtensionTimer += Time.deltaTime;
                    jumpTargetPercentage += mp.jumpArcExtensionPointsPerSecondPerSecond * jumpExtensionTimer * jumpExtensionTimer;
                }
                else if (directionChangeAngle > 135f)
                {
                    jumpExtensionTimer = 0;
                    jumpRetractionTimer += Time.deltaTime;
                    jumpTargetPercentage -= mp.jumpArcExtensionPointsPerSecondPerSecond * jumpRetractionTimer * jumpRetractionTimer;
                }
                else
                {
                    jumpExtensionTimer = 0;
                    jumpRetractionTimer = 0;
                }
                jumpTargetPercentage = Mathf.Clamp(jumpTargetPercentage, 0, 1f);
            }
            else
            {
                jumpExtensionTimer = 0;
                jumpRetractionTimer = 0;
            }
            
            //if (jumpTimer < mp.jumpImpulseExtensionTime)
            //{
            //    jumpTimer = Mathf.Min(jumpTimer + Time.deltaTime, mp.jumpImpulseExtensionTime);
            //}

            //float maximumJumpArcPercentage = mp.jumpArcInitialPercentage + ((1f - mp.jumpArcInitialPercentage) * (jumptimer / mp.jumpImpulseExtensionTime));

            float xForce = mp.minJumpImpulseForceX + mp.jumpTimingCurveX.Evaluate(jumpTargetPercentage) * mp.JumpXForceDiff;
            float yForce = mp.minJumpImpulseForceY + mp.jumpTimingCurveY.Evaluate(jumpTargetPercentage) * mp.JumpYForceDiff;
            
            Vector3 jumpForce = mp.GetJumpImpulseFromForward(forward, xForce, yForce);
            if (jumpButton)
            {
                    //wind up logic
                UpdateJumpArc(jumpForce);

                    //apply special winding up friction force
                ApplyHorizontalFrictionForce(mp.windupFrictionModifier);

            }
            else
            {
                //release jump
                rb.AddForce(jumpForce, ForceMode.Impulse);
                canJump = false;
                windingUpJump = false;
                jumpArcRenderer.StopArc();
                jumpExtensionTimer = 0;
                jumpRetractionTimer = 0;
                movementTimer = mp.jumpMovementDelay;
            }

        }
        else
        {
            if (grounded)
            {
                canJump = true;
            }
        }

        //wall climbing
        if (leftStickActivated)
        {
            RaycastHit wall;
            Vector3 start = transform.position + Vector3.down * ((box.size.y / 2f) + mp.floorDetectionDistance);
            Vector3 end = start + forward * (box.size.z / 2f + mp.wallDetectionDistance);
            int layermask = GroundMask;
            bool hit = Physics.Linecast(start, end, out wall, layermask);

            if (hit)
            {
                /*  VVV   Not sure this is necessary, but I'll keep it commented here in case VVV
                //have to make sure we're not just meeting up with another ground block 
                bool findingARealWall = true;
                if (grounded && !climbing)
                {
                    RaycastHit wall2;
                    Vector3 start2 = transform.position + Vector3.down * ((box.size.y / 2f) - mp.floorDetectionDistance);
                    Vector3 end2 = start2 + forward * (box.size.z / 2f + mp.wallDetectionDistance);
                    findingARealWall = Physics.Linecast(start2, end2, out wall2, layermask);
                }  VVV add "findingARealWall == true" to next condition*/ 

                //let's first see if we're pressing in the right direction
                if (Vector3.Angle(forward, newForward) < 90f)
                {
                    if (Falling)
                    {
                        //put our claws in the wall if we're falling, then we can start climbing
                        ApplyVerticalFrictionForce(mp.stopFrictionFactor);
                        if (!climbing)
                        {
                            EnterClimbingState();
                        }
                    }
                }
                else
                {
                    climbing = false;
                }

            } else if (wallDetectedLastFrame)
            {
                ExitClimbingState();
                rb.AddForce(forward * mp.climbUpForwardForce, ForceMode.Impulse);
                Debug.Log("Climbing forward jump thing");
            }

            wallDetectedLastFrame = hit;
        }
        else if (climbing)
        {
            ExitClimbingState();
        }


        //grounded check
        grounded = IsGrounded();
    }

    void ApplyHorizontalFrictionForce (float coefficient)
    {
        Vector3 forceDirection = new Vector3(rb.velocity.x, 0, rb.velocity.z);        
        rb.AddForce(forceDirection * -1 * coefficient, ForceMode.Force);
    }

    void ApplyVerticalFrictionForce(float coefficient)
    {
        Vector3 forceDirection = new Vector3(0, rb.velocity.y, 0);
        rb.AddForce(forceDirection * -1 * coefficient, ForceMode.Force);
    }

    void UpdateJumpArc(Vector3 jumpImpulse)
    {
        jumpArcRenderer.gameObject.SetActive(true);
        jumpArcRenderer.UpdatePointsAlongJumpArc(transform.position, rb.velocity, jumpImpulse, rb.mass);

    }

    /*void UpdateFacing()
    {
        Vector3 leftInput = im.GetLeftStickInput();
        Vector3 newForward = new Vector3(leftInput.x, 0, leftInput.y).normalized;
        forward = newForward;

        visibleMesh.transform.rotation = Quaternion.LookRotation(
           forward, Vector3.up);
    }*/

    void UpdateFacing(Vector3 newForward)
    {
        forward = newForward.normalized;
        visibleMesh.transform.rotation = Quaternion.LookRotation(
           forward, Vector3.up);
    }

    bool IsGrounded()
    {
        if (rb.velocity.y <= 0)
        {
            //Debug.Log("Grounded.");
            Vector3 start1 = transform.position + forward * (box.size.z / 2 - mp.floorDetectionDistance);
            Vector3 end1 = start1 + Vector3.down * (box.size.y / 2 + mp.floorDetectionDistance);
            Vector3 start2 = transform.position - forward * (box.size.z / 2 - mp.floorDetectionDistance);
            Vector3 end2 = start2 + Vector3.down * (box.size.y / 2 + mp.floorDetectionDistance);

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
    
    void EnterClimbingState()
    {
        climbing = true;
        galloping = false;
        trotting = false;
        grounded = false;
        movementTimer = mp.GetClimbFootstepTime(running);
    }
    void ExitClimbingState()
    {
        climbing = false;
    }

    void EnterStandingState()
    {
        galloping = false;
        trotting = false;
    }


    private void OnDrawGizmos()
    {
        if (Application.isPlaying)
        {
            Vector3 start = transform.position + Vector3.down * ((box.size.y / 2f) + mp.floorDetectionDistance);
            Vector3 end = start + forward * (box.size.z / 2f + mp.wallDetectionDistance);
            Gizmos.color = Color.cyan;
            Gizmos.DrawLine(start, end);
            
            Vector3 start2 = transform.position + Vector3.down * ((box.size.y / 2f) - mp.floorDetectionDistance);
            Vector3 end2 = start2 + forward * (box.size.z / 2f + mp.wallDetectionDistance);
            Gizmos.color = Color.blue;
            Gizmos.DrawLine(start2, end2);
        }
        
    }
    
    void Teleport()
    {
        rb.velocity = Vector3.zero;
        UpdateFacing(Vector3.forward);
    }

    private void OnTriggerEnter(Collider other)
    {
        
    }

    public void UpdateCheckpoint(Transform newCheckpoint)
    {
        previousCheckpoint = newCheckpoint;
    }

    public void RestartScene()
    {
        //TODO HACK restarting the scene completely for now
        SceneManager.LoadScene(SceneManager.GetActiveScene().buildIndex);

    }

    public void RestartFromCheckpoint()
    {
        transform.position = previousCheckpoint.position;
    }
}
