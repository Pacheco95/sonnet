#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragObjectId;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} camera;

layout(set = 0, binding = 1) uniform LightsUBO {
    vec3  direction;
    float intensity;
    vec3  color;
    float _pad;
    int   hasLight;
} light;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 baseColor = vec3(0.8, 0.8, 0.8);

    vec3 ambient = baseColor * 0.15;
    vec3 result  = ambient;

    if (light.hasLight != 0) {
        vec3 L        = normalize(-light.direction);
        vec3 N        = normalize(fragNormal);
        float NdotL   = max(dot(N, L), 0.0);

        vec3 diffuse  = baseColor * light.color * NdotL * light.intensity;

        vec3 V        = normalize(camera.cameraPos - fragWorldPos);
        vec3 H        = normalize(L + V);
        float spec    = pow(max(dot(N, H), 0.0), 64.0);
        vec3 specular = light.color * spec * light.intensity * 0.3;

        result = ambient + diffuse + specular;
    }

    outColor = vec4(result, 1.0);
}
