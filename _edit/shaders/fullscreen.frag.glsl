#version 460

layout(binding = 0) uniform sampler2D renderedImage;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(800, 600); // Use the actual window size
    outColor = texture(renderedImage, uv);
} 