# Pre-captured Vulkan triangle samples

Open them with **File > Samples** in GFXReconstruct Inspector.

- `windows-triangle.gfxr`: three frames captured on the local RTX 4080. Automatic replay uses Windows.
- `android-triangle.gfxr`: three frames captured on a Qualcomm Adreno 850 phone. Automatic replay uses an authorized Android device with the replay APK installed.
- `indexed-cube.gfxr`: three Windows frames with indexed cube geometry and a captured uniform transform. Select a draw and click Mesh to inspect solid/wireframe, local input, post-VS screen space and vertex coordinates.
- `indirect-triangles.gfxr`: three Windows frames with a three-record indexed indirect batch. In Image, choose Solid coverage/Wireframe/Depth and All or Draw 0/1/2 to inspect each triangle on the replay GPU.

Offline API/shader inspection does not require a phone. Android replay requires a
compatible Android device; platform-specific extensions can prevent local
Windows replay. Select a submitted draw, then VS or FS to view a shader. Render
frame is automatic on opening/selecting a preview; right-click a tile for actions.
File > Capture application can record new
workloads; a Vulkan test app is included in the portable tool bundle.

These captures were generated from LunarG GFXReconstruct's MIT-licensed triangle
test application. Source revision and dependency notices are recorded in the
distribution's dependency manifest and licenses directory.
