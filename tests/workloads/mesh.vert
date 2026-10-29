#version 450
layout(location=0) in vec3 position;
layout(location=0) out vec3 fragColor;
layout(set=0,binding=0,std140) uniform Scene { mat4 mvp; } scene;
void main() { gl_Position=scene.mvp*vec4(position,1.0); fragColor=position*.25+.5; }
