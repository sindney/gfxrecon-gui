#version 450
layout(set=0,binding=1) uniform sampler2D sourceTexture;
layout(location=0) in vec3 texcoord0;
layout(location=0) out vec4 color;
void main(){color=textureLod(sourceTexture,texcoord0.xy,1.0);}
