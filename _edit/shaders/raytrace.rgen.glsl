#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raycommon.h"
#include "host_device.h"

// clang-format off
layout(location = 0) rayPayloadEXT hitPayload prd;

layout(set = 0, binding = eTlas) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = eOutImage, rgba32f) uniform image2D image;
layout(set = 1, binding = eGlobals) uniform _GlobalUniforms {
    GlobalUniforms uni;
};
layout(push_constant) uniform _PushConstantRay {
    PushConstantRay pcRay;
};
// clang-format on

// Random number generation with time-based seed
float random(vec2 co, float time) {
    return fract(sin(dot(co.xy + time, vec2(12.9898, 78.233))) * 43758.5453);
}

// Generate a random point within a pixel
vec2 randomPixelOffset(vec2 pixelCenter, float time) {
    vec2 offset = vec2(random(pixelCenter + vec2(0.1), time),
        random(pixelCenter + vec2(0.2), time));
    return offset - vec2(0.5);// Center the offset around 0
}

void main() {
    const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy);
    const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);

    // Number of samples per pixel per frame
    vec3 finalColor = vec3(0.0);

    // Add random offset to pixel center for anti-aliasing
    // Use both frame count and sample index for better randomization
    vec2 offset = randomPixelOffset(pixelCenter, pcRay.time + float(pcRay.frame) * 0.1);
    vec2 d = (pixelCenter + offset) / vec2(gl_LaunchSizeEXT.xy) * 2.0 - 1.0;

    // Initialize camera ray
    vec4 origin = uni.viewInverse * vec4(0, 0, 0, 1);
    vec4 target = uni.projInverse * vec4(d.x, d.y, 1, 1);
    vec4 direction = uni.viewInverse * vec4(normalize(target.xyz), 0);

    // Initialize path tracing state
    prd.hitValue = vec3(0.0);
    prd.rayOrigin = origin.xyz;
    prd.rayDir = direction.xyz;
    prd.throughput = vec3(1.0);
    prd.depth = 0;
    prd.done = false;

    // Start path tracing loop
    while (!prd.done && prd.depth < 8) {
        // Maximum 8 bounces
        uint rayFlags = gl_RayFlagsOpaqueEXT;
        float tMin = 0.001;
        float tMax = 10000.0;

        traceRayEXT(topLevelAS, // acceleration structure
            rayFlags, // rayFlags
            0xFF, // cullMask
            0, // sbtRecordOffset
            0, // sbtRecordStride
            0, // missIndex
            prd.rayOrigin, // ray origin
            tMin, // ray min range
            prd.rayDir, // ray direction
            tMax, // ray max range
            0// payload (location = 0
        );

        // If ray hit nothing or path should terminate, break
        if (prd.done) break;
    }

    finalColor = prd.hitValue;

    // Get the current accumulated color
    vec4 currentColor = imageLoad(image, ivec2(gl_LaunchIDEXT.xy));

    // If this is the first frame, initialize with the new color
    if (pcRay.frame == 0) {
        imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(finalColor, 1.0));
    } else {
        // Accumulate the new color with the existing color
        // Use exponential moving average for better temporal stability
        float alpha = 1.0/pcRay.frame;// Adjust this value to control the accumulation rate (0.0 to 1.0)
        vec3 accumulatedColor = mix(currentColor.rgb, finalColor, alpha);
        imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(accumulatedColor, 1.0));
    }
}
