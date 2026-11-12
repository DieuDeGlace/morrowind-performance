#version 120

#include "lib/core/fragment.h.glsl"

uniform sampler2D reflectionMap;
uniform Material material;

vec4 sampleReflectionMap(vec2 uv)
{
    return texture2D(reflectionMap, uv);
}

uniform sampler2D lastShader;

vec4 samplerLastShader(vec2 uv)
{
    return texture2D(lastShader, uv);
}

#if @skyBlending
uniform sampler2D sky;

vec3 sampleSkyColor(vec2 uv)
{
    return texture2D(sky, uv).xyz;
}
#endif

// OPENMW_ANDROID_053_PACKED_OPAQUE_DEPTH
// The opaque depth arrives packed into RGB as a 24-bit integer (see
// opaqueblit.cpp); full precision is needed to unpack it exactly.
#if defined(GL_ES) && defined(GL_FRAGMENT_PRECISION_HIGH)
uniform highp sampler2D opaqueDepthTex;
#else
uniform sampler2D opaqueDepthTex;
#endif
uniform sampler2D opaqueColorTex;

vec4 sampleOpaqueDepthTex(vec2 uv)
{
    vec3 bytes = floor(texture2D(opaqueDepthTex, uv).rgb * 255.0 + vec3(0.5));
    float depth = dot(bytes, vec3(65536.0, 256.0, 1.0)) / 16777215.0;
    return vec4(depth);
}

vec4 sampleOpaqueColorTex(vec2 uv)
{
    return texture2D(opaqueColorTex, uv);
}

Material getMaterial()
{
    return material;
}
