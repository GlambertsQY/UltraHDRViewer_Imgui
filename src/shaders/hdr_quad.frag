#version 450 core

// HDR image output shader.
// Input texture: R16G16B16A16_SFLOAT, linear BT.709, 1.0 == whiteNits nits.
// mode 0 (scRGB): linear output scaled to 80-nits reference white.
// mode 1 (HDR10): BT.709 -> BT.2020 gamut conversion + ST2084 PQ encoding.

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
    vec2 rectMin;
    vec2 rectMax;
    vec2 fbSize;
    int mode;
    float whiteNits;
    float refWhite;
    float pad;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// BT.709 linear -> BT.2020 linear (ITU-R BT.2087), GLSL mat3 is column-major
const mat3 M709_2020 = mat3(0.6274039, 0.0690970, 0.0163914,   // column 0
                            0.3292830, 0.9195404, 0.0880132,   // column 1
                            0.0433000, 0.0113600, 0.8956000);  // column 2

float pqEncode(float nits) {
    float y = clamp(nits / 10000.0, 0.0, 1.0);
    float ym = pow(y, 0.1593017578125);
    return pow((0.8359375 + 18.8515625 * ym) / (1.0 + 18.6875 * ym), 78.84375);
}

void main() {
    vec3 lin = texture(uTex, vUV).rgb;
    vec3 rgb;
    if (pc.mode == 0) {
        rgb = lin * (pc.whiteNits / pc.refWhite);
    } else {
        vec3 c2020 = M709_2020 * (lin * pc.whiteNits);
        rgb = vec3(pqEncode(c2020.r), pqEncode(c2020.g), pqEncode(c2020.b));
    }
    outColor = vec4(max(rgb, vec3(0.0)), 1.0);
}
