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

// Unpacking sums the bytes up to 16777215, which overflows 16-bit floats, so
// the sum is always done in high precision. When shaders run at medium
// precision (the launcher's "Medium shader precision" option, or a GPU
// without high precision) depth near the far plane is too coarse for shore
// fading anyway; report no opaque depth there, i.e. deep open water.
vec4 sampleOpaqueDepthTex(vec2 uv)
{
#if @opaqueDepthDisabled || (defined(GL_ES) && !defined(GL_FRAGMENT_PRECISION_HIGH))
    return vec4(1.0);
#else
#if defined(GL_ES)
    highp vec3 bytes = floor(texture2D(opaqueDepthTex, uv).rgb * 255.0 + vec3(0.5));
    highp float depth = dot(bytes, vec3(65536.0, 256.0, 1.0)) / 16777215.0;
#else
    vec3 bytes = floor(texture2D(opaqueDepthTex, uv).rgb * 255.0 + vec3(0.5));
    float depth = dot(bytes, vec3(65536.0, 256.0, 1.0)) / 16777215.0;
#endif
    return vec4(depth);
#endif
}

// OPENMW_ANDROID_053_WATER_DEBUG_READBACK
// The packed texel exactly as sampled, and which of the branches above this
// shader compiled, so the water diagnostic can read what the GPU saw.
vec4 sampleOpaqueDepthTexRaw(vec2 uv)
{
    return texture2D(opaqueDepthTex, uv);
}

float opaqueDepthDebugFlags()
{
    float flags = 0.0;
#if defined(GL_ES) && defined(GL_FRAGMENT_PRECISION_HIGH)
    flags += 128.0;
#endif
#if @opaqueDepthDisabled
    flags += 64.0;
#endif
#if defined(GL_ES)
    flags += 8.0;
#endif
    return flags;
}

vec4 sampleOpaqueColorTex(vec2 uv)
{
    return texture2D(opaqueColorTex, uv);
}

Material getMaterial()
{
    return material;
}
