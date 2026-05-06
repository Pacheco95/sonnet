#version 450

layout(location = 0) in flat uint fragObjectId;

layout(location = 0) out vec4 outColor;

void main() {
    uint id = fragObjectId;
    outColor = vec4(
        float((id >> 16u) & 0xFFu) / 255.0,
        float((id >>  8u) & 0xFFu) / 255.0,
        float( id         & 0xFFu) / 255.0,
        1.0
    );
}
