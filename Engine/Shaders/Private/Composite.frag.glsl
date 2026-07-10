#version 460

// CompositePass: samples the offscreen scene color into the swapchain
// (Stage 2 §6.e - the minimal combined-image-sampler path, ADR-0024).

layout(set = 0, binding = 0) uniform sampler2D u_scene_color;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_scene_color, v_uv);
}
