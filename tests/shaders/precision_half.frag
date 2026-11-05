#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
layout(location=0) out vec4 color;
layout(push_constant) uniform Control { float seed; uint iterations; } control;
void main() {
    f16vec4 base = f16vec4(vec4(gl_FragCoord.xy / 512.0, control.seed, 0.375));
    f16vec4 a = base, b = base.yzwx, c = base.zwxy, d = base.wxyz;
    for (uint i=0; i<control.iterations; ++i) {
        a = fract(a * f16vec4(1.125,0.75,1.5,0.5) + b * float16_t(0.03125) + float16_t(0.0625));
        b = fract(b * f16vec4(0.75,1.5,0.5,1.125) + c * float16_t(0.0625) + float16_t(0.125));
        c = fract(c * f16vec4(1.5,0.5,1.125,0.75) + d * float16_t(0.125) + float16_t(0.03125));
        d = fract(d * f16vec4(0.5,1.125,0.75,1.5) + a * float16_t(0.03125) + float16_t(0.25));
    }
    color = vec4(fract(vec3(a.xyz+b.xyz+c.xyz+d.xyz) * 0.25), 1.0);
}
