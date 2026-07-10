#version 460

// Fullscreen triangle from gl_VertexIndex - no vertex buffer.
// Covers the viewport with vertices (-1,-1) (3,-1) (-1,3).

layout(location = 0) out vec2 v_uv;

void main() {
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 pos = positions[gl_VertexIndex];
    v_uv        = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
