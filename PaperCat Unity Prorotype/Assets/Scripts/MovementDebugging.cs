using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class MovementDebugging : MonoBehaviour
{
    public GameObject ballPrefab;
    public bool active = true;
    public float ballDropTiming = 0.5f;
    float ballDropTimer = 0;
    Vector3 prevPosition;
    // Start is called before the first frame update
    void Start()
    {
        if (active && ballPrefab == null)
        {
            Debug.LogWarning("trying to debug without a ball drop prefab");
        }
    }

    // Update is called once per frame
    void Update()
    {
        if (!active)
            return;

        ballDropTimer += Time.deltaTime;
        if (ballDropTimer > ballDropTiming)
        {
            GameObject.Instantiate(ballPrefab, this.transform.position, this.transform.rotation);
            ballDropTimer = 0;
        }
    }
}
