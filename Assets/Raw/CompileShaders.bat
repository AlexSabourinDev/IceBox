cd /D "%~dp0"
..\..\SDK\DXC\bin\dxc.exe -spirv -T vs_6_6 -E vertexMain SampleForward.hlsl -fspv-target-env=vulkan1.0 -Fo SampleForwardVert.spv
..\..\SDK\DXC\bin\dxc.exe -spirv -T ps_6_6 -E fragMain SampleForward.hlsl -fspv-target-env=vulkan1.0 -Fo SampleForwardFrag.spv
timeout 10