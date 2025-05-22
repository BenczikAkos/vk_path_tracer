
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.h"
#include "wavefront.h"

hitAttributeEXT vec2 attribs;

layout(location = 0) rayPayloadInEXT hitPayload prd;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout(buffer_reference, scalar) buffer Vertices {
    Vertex v[];
}; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {
    ivec3 i[];
}; // Triangle indices
layout(buffer_reference, scalar) buffer Materials {
    WaveFrontMaterial m[];
}; // Array of all materials on an object
layout(buffer_reference, scalar) buffer MatIndices {
    int i[];
}; // Material ID for each triangle
layout(set = 0, binding = eTlas) uniform accelerationStructureEXT topLevelAS;
layout(set = 1, binding = eObjDescs, scalar) buffer ObjDesc_ {
    ObjDesc i[];
} objDesc;
layout(set = 1, binding = eTextures) uniform sampler2D textureSamplers[];

layout(push_constant) uniform _PushConstantRay {
    PushConstantRay pcRay;
};

// Random number generation with time-based seed
float random(vec2 co, float time) {
    return fract(sin(dot(co.xy + time, vec2(12.9898,78.233))) * 43758.5453);
}

vec3 randomDirection(vec3 normal, vec2 rand, float time) {
    float theta = 2.0 * 3.14159265359 * rand.x;
    float phi = acos(sqrt(rand.y));
    float sinPhi = sin(phi);

    vec3 tangent = normalize(cross(normal, abs(normal.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * (sinPhi * cos(theta)) + 
        bitangent * (sinPhi * sin(theta)) + 
        normal * cos(phi));
}

void main() {
    // Object data
    ObjDesc    objResource = objDesc.i[gl_InstanceCustomIndexEXT];
    MatIndices matIndices  = MatIndices(objResource.materialIndexAddress);
    Materials  materials   = Materials(objResource.materialAddress);
    Indices    indices     = Indices(objResource.indexAddress);
    Vertices   vertices    = Vertices(objResource.vertexAddress);

    // Indices of the triangle
    ivec3 ind = indices.i[gl_PrimitiveID];

    // Vertex of the triangle
    Vertex v0 = vertices.v[ind.x];
    Vertex v1 = vertices.v[ind.y];
    Vertex v2 = vertices.v[ind.z];

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    // Computing the coordinates of the hit position
    const vec3 pos      = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));

    // Computing the normal at hit position
    const vec3 nrm      = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + v2.nrm * barycentrics.z;
    const vec3 worldNrm = normalize(vec3(nrm * gl_WorldToObjectEXT));

    // Material of the object
    int               matIdx = matIndices.i[gl_PrimitiveID];
    WaveFrontMaterial mat    = materials.m[matIdx];

    // Get material color
    vec3 albedo = mat.diffuse;
    if(mat.textureId >= 0) {
        uint txtId    = mat.textureId + objDesc.i[gl_InstanceCustomIndexEXT].txtOffset;
        vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
        albedo *= texture(textureSamplers[nonuniformEXT(txtId)], texCoord).xyz;
    }

    // Russian roulette for path termination
    float p = max(albedo.x, max(albedo.y, albedo.z));
    if (prd.depth > 3) {
        if (random(vec2(gl_LaunchIDEXT.xy) + vec2(prd.depth), pcRay.time) > p) {
            prd.done = true;
            return;
        }
        prd.throughput /= p;
    }

    // Direct lighting contribution
    vec3 L = normalize(pcRay.lightPosition - worldPos);
    float lightDistance = length(pcRay.lightPosition - worldPos);
    float lightIntensity = pcRay.lightIntensity / (lightDistance * lightDistance);

    // Check if light is visible
    float tMin = 0.001;
    float tMax = lightDistance;
    vec3 origin = worldPos;
    vec3 rayDir = L;
    uint flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
    isShadowed = true;
    traceRayEXT(topLevelAS, flags, 0xFF, 0, 0, 1, origin, tMin, rayDir, tMax, 1);
    float shadowCoeff = isShadowed ? 0.3f : 1.0f;
    // Add direct lighting contribution if light is visible
    vec3 viewDir = worldPos - pcRay.cameraPosition;
    prd.hitValue += computeSpecular(mat, viewDir, L, worldNrm) * shadowCoeff * lightIntensity;

    // Generate next ray direction using importance sampling
    vec2 rand = vec2(random(vec2(gl_LaunchIDEXT.xy) + vec2(prd.depth), pcRay.time),
        random(vec2(gl_LaunchIDEXT.xy) + vec2(prd.depth + 1), pcRay.time));
    vec3 nextDir = randomDirection(worldNrm, rand, pcRay.time);

    // Update path state
    prd.rayOrigin = worldPos;
    prd.rayDir = nextDir;
    prd.throughput *= albedo;
    prd.depth++;
}
