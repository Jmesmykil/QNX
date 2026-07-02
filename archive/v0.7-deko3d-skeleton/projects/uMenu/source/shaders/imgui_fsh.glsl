// imgui_fsh.glsl — ImGui fragment shader for deko3d / Q OS uMenu v0.7
// Adapted from ftpd/mtheall (MIT)
// Compile: uam -s frag -o imgui_fsh.dksh imgui_fsh.glsl
#version 460

layout (location = 0) in vec2 vtxUv;
layout (location = 1) in vec4 vtxColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform FragUBO {
    uint font;
} ubo;

layout (location = 0) out vec4 outColor;

void main() {
    // Font atlas is single-channel R8; apply as alpha modulate.
    if (ubo.font != 0)
        outColor = vtxColor * vec4(vec3(1.0), texture(tex, vtxUv).r);
    else
        outColor = vtxColor * texture(tex, vtxUv);
}
