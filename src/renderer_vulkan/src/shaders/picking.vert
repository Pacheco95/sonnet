#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} camera;

layout(push_constant) uniform PushConstants {
    mat4  model;
    uint  objectId;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

layout(location = 0) out flat uint fragObjectId;

void main() {
    fragObjectId = pc.objectId;
    gl_Position  = camera.proj * camera.view * pc.model * vec4(inPosition, 1.0);
}
