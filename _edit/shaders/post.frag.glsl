#version 450
layout(location = 0) in vec2 outUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D noisyTxt;

layout(push_constant) uniform shaderInformation {
    float aspectRatio;
} pushc;

// Parameters for bilateral filter
#define KERNEL_RADIUS 2 // Defines the size of the pixel window (KERNEL_RADIUS*2+1)^2
#define SPATIAL_SIGMA 3.0 // Controls spatial influence (larger blurs more)
#define RANGE_SIGMA 0.1   // Controls color similarity influence (smaller is more sensitive to color differences)

// Gaussian function
float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    vec2 uv = outUV;
    vec2 texelSize = 1.0 / vec2(textureSize(noisyTxt, 0)); // Size of one texel

    vec3 centerColor = texture(noisyTxt, uv).rgb;
    vec3 filteredColor = vec3(0.0);
    float totalWeight = 0.0;

    for (int x = -KERNEL_RADIUS; x <= KERNEL_RADIUS; ++x) {
        for (int y = -KERNEL_RADIUS; y <= KERNEL_RADIUS; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec3 neighborColor = texture(noisyTxt, uv + offset).rgb;

            // Spatial weight
            float distSq = dot(offset / texelSize, offset / texelSize); // Use pixel distance
            float weightSpatial = gaussian(sqrt(distSq), SPATIAL_SIGMA);

            // Range (color similarity) weight
            // Using luminance difference for simplicity, could use full RGB distance
            float lumaCenter = dot(centerColor, vec3(0.299, 0.587, 0.114));
            float lumaNeighbor = dot(neighborColor, vec3(0.299, 0.587, 0.114));
            float diffRange = abs(lumaCenter - lumaNeighbor);
            // Alternatively, for color distance: float diffRange = distance(centerColor, neighborColor);
            float weightRange = gaussian(diffRange, RANGE_SIGMA);

            float weight = weightSpatial * weightRange;

            filteredColor += neighborColor * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0) {
        filteredColor /= totalWeight;
    } else {
        filteredColor = centerColor; // Should not happen with proper weights
    }

    float gamma = 1.0 / 2.2;
    fragColor = pow(vec4(filteredColor, 1.0), vec4(gamma));
}

