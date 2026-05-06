#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D maskSampler;

layout(push_constant) uniform PushConstants {
    vec3  outlineColor;
    float _pad;
    vec2  texelSize;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    float center = texture(maskSampler, fragUV).r;
    if (center > 0.5) {
        discard;
    }

    float edge = 0.0;
    for (int r = 1; r <= 6; ++r) {
        float rF = float(r);
        edge += texture(maskSampler, fragUV + vec2( rF * pc.texelSize.x, 0.0)).r;
        edge += texture(maskSampler, fragUV + vec2(-rF * pc.texelSize.x, 0.0)).r;
        edge += texture(maskSampler, fragUV + vec2(0.0,  rF * pc.texelSize.y)).r;
        edge += texture(maskSampler, fragUV + vec2(0.0, -rF * pc.texelSize.y)).r;
    }

    if (edge > 0.0) {
        outColor = vec4(pc.outlineColor, 1.0);
    } else {
        discard;
    }
}
