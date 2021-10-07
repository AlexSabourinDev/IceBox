using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class ResetManager : MonoBehaviour
{
    public static ResetManager instance;
    public static ResetManager Instance
    {
        get
        {
            return instance;
        }
    }
    ResettableObject[] chilluns;
    bool[] initialStates;
    // Start is called before the first frame update
    void Awake()
    {
        instance = this;
        chilluns = GetComponentsInChildren<ResettableObject>();
        initialStates = new bool[chilluns.Length];

        for (int i = 0; i < chilluns.Length; i++)
        {
            initialStates[i] = chilluns[i].gameObject.activeSelf;
        }

    }

    // Update is called once per frame
    void Update()
    {
        
    }

    public void ResetAll()
    {
        for (int i = 0; i < chilluns.Length; i++)
        {
            chilluns[i].Reset();
            chilluns[i].gameObject.SetActive(initialStates[i]);
        }
    }

    public void EnableAll()
    {
        for (int i = 0; i < chilluns.Length; i++)
        {
            chilluns[i].enabled = true;
            chilluns[i].gameObject.SetActive(true);
        }
    }

    public void DisableAll()
    {
        for (int i = 0; i < chilluns.Length; i++)
        {
            chilluns[i].enabled = false;
            chilluns[i].gameObject.SetActive(false);
        }
    }

    public void DisableChild(GameObject child)
    {
        ResettableObject ro = child.GetComponent<ResettableObject>();

        if (ro == null)
        {
            Debug.LogWarning("Trying to disable a child without a resettable object compoent");
            return;
        }

        ro.enabled = false;
        child.SetActive(false);
    }
}
