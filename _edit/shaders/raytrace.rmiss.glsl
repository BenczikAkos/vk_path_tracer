
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raycommon.h"
#include "wavefront.h"

layout(location = 0) rayPayloadInEXT hitPayload prd;

layout(push_constant) uniform _PushConstantRay {
    PushConstantRay pcRay;
};

void main() {
    vec3 skyColor = pcRay.clearColor.xyz * 0.8;
    prd.hitValue += prd.throughput * skyColor;
    prd.done = true;
}
