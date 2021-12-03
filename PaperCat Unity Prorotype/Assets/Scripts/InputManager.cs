using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.InputSystem;

public class InputManager : MonoBehaviour
{
    public bool rotateForCamera = true;

    Gamepad gp;
    // Start is called before the first frame update
    void Start()
    {
       
    }

    // Update is called once per frame
    void Update()
    {
        
    }


    public Vector2 GetLeftStickInput()
    {
        
        gp = Gamepad.current;
        Vector2 input = gp.leftStick.ReadValue();
        if (rotateForCamera)
        {       //TODO : gotta put the y input on z before rotating
            Debug.Log("input going in: " + input);
            float facing = Camera.main.transform.eulerAngles.y;
            Debug.Log("facing: " + facing);
            Vector3 rotatedInput = Quaternion.Euler(0, facing, 0) * new Vector3(input.x, 0, input.y);
            Debug.Log("input going out: " + input);
            input = new Vector2(rotatedInput.x, rotatedInput.z);
        }
        return input;
    }

    public bool GetSouthButton()
    {
        gp = Gamepad.current;
        bool input = gp.buttonSouth.isPressed;
        return input;
    }

    public bool GetSouthButtonDown()
    {
        gp = Gamepad.current;
        bool input = gp.buttonSouth.wasPressedThisFrame;
        return input;
    }

    public bool GetAnyLeftShoulder()
    {
        gp = Gamepad.current;
        bool input = gp.leftTrigger.ReadValue() > 0 || gp.leftShoulder.ReadValue() > 0;
        return input;
    }
}
