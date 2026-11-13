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
// The opaque depth arrives packed into RGB as a 24-bit integer, written into
// the spare opaque colour copy (OPENMW_ANDROID_053_DEPTH_IN_SPARE_COLOR_COPY
// in opaqueblit.cpp). Each byte only needs 8 bits, so the sampler keeps the
// default precision; the bytes are summed in high precision below.
uniform sampler2D opaqueDepthTex;
uniform sampler2D opaqueColorTex;
// Size of the packed depth copy, or zero when unknown. The copy is linearly
// filtered like every colour copy, so reads are snapped to texel centres to
// keep the packed bytes of neighbouring texels apart.
uniform vec2 opaqueDepthTexSize;

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
    highp vec2 size = opaqueDepthTexSize;
    highp vec2 texelUv = uv;
    if (size.x > 0.0)
        texelUv = (floor(texelUv * size) + vec2(0.5)) / size;
    highp vec3 bytes = floor(texture2D(opaqueDepthTex, texelUv).rgb * 255.0 + vec3(0.5));
    highp float depth = dot(bytes, vec3(65536.0, 256.0, 1.0)) / 16777215.0;
#else
    vec2 texelUv = uv;
    if (opaqueDepthTexSize.x > 0.0)
        texelUv = (floor(texelUv * opaqueDepthTexSize) + vec2(0.5)) / opaqueDepthTexSize;
    vec3 bytes = floor(texture2D(opaqueDepthTex, texelUv).rgb * 255.0 + vec3(0.5));
    float depth = dot(bytes, vec3(65536.0, 256.0, 1.0)) / 16777215.0;
#endif
    return vec4(depth);
#endif
}

vec4 sampleOpaqueColorTex(vec2 uv)
{
    return texture2D(opaqueColorTex, uv);
}

Material getMaterial()
{
    return material;
}
