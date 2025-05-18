#version 460

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() 
{
    // Generate colors based on UV coordinates
    outColor = vec4(inUV.x, inUV.y, 1.0 - inUV.x * inUV.y, 1.0);
} 