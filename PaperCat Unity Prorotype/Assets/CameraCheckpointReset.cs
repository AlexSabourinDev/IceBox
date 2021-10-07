using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Cinemachine;

public class CameraCheckpointReset : MonoBehaviour
{
    public static CameraCheckpointReset Instance;
    CinemachineVirtualCamera cam;
    CinemachineTrackedDolly dolly;
    float prevCheckpointTrackPosition = 0;

    private void Awake()
    {
        if (Instance != null)
        {
            Destroy(Instance.gameObject);
            Debug.Log("What is this camera doing here? I'm the camera!");
        }
        Instance = this;
    }

    void Start()
    {
        
        cam = GetComponent<CinemachineVirtualCamera>();
        dolly = cam.GetCinemachineComponent<CinemachineTrackedDolly>();
        SaveCheckpointPosition();
    }
    

    public void SaveCheckpointPosition()
    {
        prevCheckpointTrackPosition = dolly.m_PathPosition;
    }

    public void LoadCheckpointPosition()
    {
        dolly.m_PathPosition = prevCheckpointTrackPosition;
    }

}
