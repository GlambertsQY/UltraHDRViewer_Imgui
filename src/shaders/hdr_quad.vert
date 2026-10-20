#version 450 core

// HDR image quad: rectangle given in framebuffer pixels via push constants.
// NDC mapping matches ImGui's Vulkan backend (positive-height viewport,
// y down: NDC y = -1 is the top of the framebuffer).

layout(push_constant) uniform PC {
    vec2 rectMin;
    vec2 rectMax;
    vec2 fbSize;
    int mode;        // 0 = scRGB, 1 = HDR10
    float whiteNits; // HDRData 1.0 == whiteNits nits (203)
    float refWhite;  // scRGB reference white nits (80)
    float pad;
} pc;

layout(location = 0) out vec2 vUV;

void main() {
    vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1),
                             vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 k = corners[gl_VertexIndex];
    vUV = k;
    vec2 px = mix(pc.rectMin, pc.rectMax, k);
    gl_Position = vec4(px / pc.fbSize * 2.0 - 1.0, 0.0, 1.0);
}
