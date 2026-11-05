#version 450
layout(location=0) out vec4 color;
layout(push_constant) uniform Control { float seed; uint iterations; } control;
void main() {
    vec4 base = vec4(gl_FragCoord.xy / 512.0, control.seed, 0.375);
    vec4 a = base, b = base.yzwx, c = base.zwxy, d = base.wxyz;
    // Runtime loop count and fragment coordinates prevent constant folding.
    for (uint i=0; i<control.iterations; ++i) {
        a = fract(a * vec4(1.125,0.75,1.5,0.5) + b * 0.03125 + 0.0625);
        b = fract(b * vec4(0.75,1.5,0.5,1.125) + c * 0.0625 + 0.125);
        c = fract(c * vec4(1.5,0.5,1.125,0.75) + d * 0.125 + 0.03125);
        d = fract(d * vec4(0.5,1.125,0.75,1.5) + a * 0.03125 + 0.25);
    }
    color = vec4(fract((a+b+c+d).xyz * 0.25), 1.0);
}
