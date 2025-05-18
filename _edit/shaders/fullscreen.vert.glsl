#version 460

void main()
{
    // Generate a fullscreen triangle using vertex ID
    float x = float((gl_VertexIndex & 1) << 2) - 1.0;
    float y = float((gl_VertexIndex & 2) << 1) - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
} 