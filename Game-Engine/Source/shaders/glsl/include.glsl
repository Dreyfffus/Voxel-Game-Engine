layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunDirection; // w component unused
    vec4 sunColor;
} sceneData;

layout (set = 1, binding = 0) uniform GLTFMaterialData {
    vec4 colorFactors; // RGBA
    vec4 metallicRoughnessFactors; // Metallic, Roughness, unused, unused
} materialData;

layout (set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout (set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;