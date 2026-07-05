#version 450

// World-space text panel: bufferless two-triangle quad centered on its
// model origin in the XY plane. Emits panel-space text coordinates, pixels y-down.

layout(push_constant) uniform TextWorldPush {
    mat4  mvp;    // proj * view * model
    vec4  panel;  // panel pixel W,H | panel world W,H
    uvec2 range;  // first instance index, instance count
} pc;

layout(location = 0) out vec2 panelPos;

const vec2 uvs[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                            vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main()
{
    vec2 uv = uvs[gl_VertexIndex];
    panelPos = uv * pc.panel.xy;
    vec3 local = vec3((uv.x - 0.5) * pc.panel.z, (0.5 - uv.y) * pc.panel.w, 0.0);
    gl_Position = pc.mvp * vec4(local, 1.0);
}
