using System.Collections;
using System.Collections.Generic;
using UnityEngine;

[RequireComponent(typeof(Rigidbody))]
public class CatMovementParameters : MonoBehaviour
{
    [Header("Trot parameters")]
    public float trotImpulseForceX = 5f;
    public float trotImpulseForceY = 0.5f;
    public float trotMaintenanceForceX = 5f;
    public float trotImpulseTiming = 0.15f;
    public float trotFootstepTime = 0.04f;
    public float trotTurnaroundTime = 0.1f;
    public float trotHighSpeedThreshold = 0.8f;
    
    [Header("Gallop parameters")]
    public float gallopImpulseForceX = 5f;
    public float gallopImpulseForceY = 0.5f;
    public float gallopMaintenanceForceX = 8f;
    public float gallopImpulseTiming = 0.3f;
    public float gallopFootstepTime = 0.08f;
    public float gallopTurnaroundTime = 0.2f;
    public float gallopHighSpeedThreshold = 3f;

    [Header("Jump parameters")]
    public float minJumpImpulseForceX = 5f;
    public float maxJumpImpulseForceX = 15f;
    public float minJumpImpulseForceY = 3f;
    public float maxJumpImpulseForceY = 10f;
    public float jumpImpulseExtensionTime = 1f;
    public float jumpImpulseRetractionTime = 1f;
    public float jumpTargetAdjustmentRate = 0.1f;
    public AnimationCurve jumpTimingCurveX;
    public AnimationCurve jumpTimingCurveY;
    public float landingRecoverTime = 0.15f;
    public float windupFrictionModifier = 4f;
    public float jumpMovementDelay = 0.8f;
    public float jumpArcInitialPercentage = 0.5f;
    public float jumpArcRotationRadiansPerSecond = 1.4f;
    public float jumpArcExtensionPointsPerSecondPerSecond = 0.25f;

    [Header("Climbing")]
    public float wallDetectionDistance = 0.1f;
    public float climbForce = 2f;
    public float climbUpForwardForce = 8f;
    public float slowClimbTiming = 0.2f;
    public float slowClimbFootstepTime = 0.1f;
    public float slowClimbImpulseForce = 3f;
    public float fastClimbForce = 3f;
    public float fastClimbTiming = 0.3f;
    public float fastClimbFootstepTime = 0.12f;
    public float fastClimbImpulseForce = 5f;
    public float climbStopFrictionFactor = 15f;
    public float minimumYVelocityToStartClimbing = 0.5f;

    [Header("Other")]
    public float movingFrictionFactor = 0.2f;
    public float stopFrictionFactor = 0.8f;
    public float jumpStopForceFactor = 4f;
    public float decelerationMagnitudeThreshold = 1f;
    public float stopAndTurnMinimumAngle = 5f;
    public float floorDetectionDistance = 0.1f;
    public float dragOnGroundWhileMoving = 4f;

    public float JumpXForceDiff
    {
        get
        {
            return maxJumpImpulseForceX - minJumpImpulseForceX;
        }
    }

    public float JumpYForceDiff
    {
        get
        {
            return maxJumpImpulseForceY - minJumpImpulseForceY;
        }
    }

    public float TotalJumpTiming
    {
        get
        {
            return jumpImpulseExtensionTime + jumpImpulseRetractionTime;
        }
    }

    public float GetFootstepTime(bool running)
    {
        return running ? gallopFootstepTime : trotFootstepTime;
    }


    public float GetClimbFootstepTime(bool running)
    {
        return running ? fastClimbFootstepTime : slowClimbFootstepTime;
    }


    public float GetClimbMovementTiming(bool running)
    {
        return running ? fastClimbTiming : slowClimbTiming;
    }


    public float GetClimbForce(bool running)
    {
        return running? fastClimbImpulseForce : slowClimbImpulseForce;
    }


    public Vector3 GetJumpImpulseFromForward(Vector3 fore, float xForce, float yForce)
    {
        Vector3 forceVector = new Vector3(xForce, 0, 0);
        Vector3 adjustedForwardForce = Vector3.RotateTowards(forceVector, fore, 4f, 0f);
        return new Vector3(adjustedForwardForce.x, yForce, adjustedForwardForce.z);
    }

    public float GetHorizontalForceMagnitude(bool running, bool highSpeed)
    {
        if (running)
        {
            return highSpeed ? gallopMaintenanceForceX : gallopImpulseForceX;
        }
        else
        {
            return highSpeed ? trotMaintenanceForceX : trotImpulseForceX;
        }
    }

    
    //[Header("General physics")]
    //public float friction = 0.5f;

    // Start is called before the first frame update
    void Start()
    {
    }

    // Update is called once per frame
    void Update()
    {
        
    }
}
