using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.InputSystem;

public class InputManager : MonoBehaviour
{
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
