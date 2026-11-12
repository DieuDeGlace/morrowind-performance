#define SHADOWS @shadows_enabled

#if SHADOWS

#ifndef PER_PIXEL_LIGHTING
#define PER_PIXEL_LIGHTING 0
#endif

    uniform float maximumShadowMapDistance;
    uniform float shadowFadeStart;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        uniform sampler2DShadow shadowTexture@shadow_texture_unit_index;
        varying vec4 shadowSpaceCoords@shadow_texture_unit_index;

#if @perspectiveShadowMaps
        varying vec4 shadowRegionCoords@shadow_texture_unit_index;
#endif
    @endforeach

// ============================================================================
// Soft Shadow Filtering (contiguous texel block, see below)
// ============================================================================
#if @softShadows

// Soft shadows read a contiguous block of shadow-map texels with weights
// that change smoothly as the receiver moves within a texel (the
// "optimized PCF" weighting from Ignacio Castaño's The Witness shadow
// filtering, as in MJP's Shadows sample). Each hardware-filtered lookup
// covers 2x2 texels, so 4, 9 or 16 lookups give a 3x3, 5x5 or 7x7 texel
// filter. Because every texel under the filter is read, a thin caster such
// as a leaf cannot fall between samples: when the shadow map shifts as the
// camera moves, its shadow keeps the same softness instead of flickering.

#define SHADOWMAP_RES @shadowMapResolution

float sampleShadowTexels(sampler2DShadow tex, vec2 base, float u, float v, float depth)
{
    return shadow2D(tex, vec3(base + vec2(u, v) / SHADOWMAP_RES, depth)).r;
}

float getFilteredShadowing(sampler2DShadow tex, vec4 coord)
{
    vec3 shadowUV = coord.xyz / coord.w;
    float depth = shadowUV.z;

    vec2 texels = shadowUV.xy * SHADOWMAP_RES;
    vec2 base = floor(texels + vec2(0.5));
    float s = texels.x + 0.5 - base.x;
    float t = texels.y + 0.5 - base.y;
    base = (base - vec2(0.5)) / SHADOWMAP_RES;

    // soft shadow radius picks the filter width: 0.5 is 3x3 texels,
    // 1.0 (default) 5x5, 1.5 and wider 7x7.
    const float radius = @softShadowRadius;
    float sum = 0.0;
    if (radius < 0.75)
    {
        float uw0 = 3.0 - 2.0 * s;
        float uw1 = 1.0 + 2.0 * s;
        float u0 = (2.0 - s) / uw0 - 1.0;
        float u1 = s / uw1 + 1.0;
        float vw0 = 3.0 - 2.0 * t;
        float vw1 = 1.0 + 2.0 * t;
        float v0 = (2.0 - t) / vw0 - 1.0;
        float v1 = t / vw1 + 1.0;

        sum += uw0 * vw0 * sampleShadowTexels(tex, base, u0, v0, depth);
        sum += uw1 * vw0 * sampleShadowTexels(tex, base, u1, v0, depth);
        sum += uw0 * vw1 * sampleShadowTexels(tex, base, u0, v1, depth);
        sum += uw1 * vw1 * sampleShadowTexels(tex, base, u1, v1, depth);
        return sum / 16.0;
    }
    if (radius < 1.25)
    {
        float uw0 = 4.0 - 3.0 * s;
        float uw1 = 7.0;
        float uw2 = 1.0 + 3.0 * s;
        float u0 = (3.0 - 2.0 * s) / uw0 - 2.0;
        float u1 = (3.0 + s) / uw1;
        float u2 = s / uw2 + 2.0;
        float vw0 = 4.0 - 3.0 * t;
        float vw1 = 7.0;
        float vw2 = 1.0 + 3.0 * t;
        float v0 = (3.0 - 2.0 * t) / vw0 - 2.0;
        float v1 = (3.0 + t) / vw1;
        float v2 = t / vw2 + 2.0;

        sum += uw0 * vw0 * sampleShadowTexels(tex, base, u0, v0, depth);
        sum += uw1 * vw0 * sampleShadowTexels(tex, base, u1, v0, depth);
        sum += uw2 * vw0 * sampleShadowTexels(tex, base, u2, v0, depth);
        sum += uw0 * vw1 * sampleShadowTexels(tex, base, u0, v1, depth);
        sum += uw1 * vw1 * sampleShadowTexels(tex, base, u1, v1, depth);
        sum += uw2 * vw1 * sampleShadowTexels(tex, base, u2, v1, depth);
        sum += uw0 * vw2 * sampleShadowTexels(tex, base, u0, v2, depth);
        sum += uw1 * vw2 * sampleShadowTexels(tex, base, u1, v2, depth);
        sum += uw2 * vw2 * sampleShadowTexels(tex, base, u2, v2, depth);
        return sum / 144.0;
    }

    float uw0 = 5.0 * s - 6.0;
    float uw1 = 11.0 * s - 28.0;
    float uw2 = -(11.0 * s + 17.0);
    float uw3 = -(5.0 * s + 1.0);
    float u0 = (4.0 * s - 5.0) / uw0 - 3.0;
    float u1 = (4.0 * s - 16.0) / uw1 - 1.0;
    float u2 = -(7.0 * s + 5.0) / uw2 + 1.0;
    float u3 = -s / uw3 + 3.0;
    float vw0 = 5.0 * t - 6.0;
    float vw1 = 11.0 * t - 28.0;
    float vw2 = -(11.0 * t + 17.0);
    float vw3 = -(5.0 * t + 1.0);
    float v0 = (4.0 * t - 5.0) / vw0 - 3.0;
    float v1 = (4.0 * t - 16.0) / vw1 - 1.0;
    float v2 = -(7.0 * t + 5.0) / vw2 + 1.0;
    float v3 = -t / vw3 + 3.0;

    sum += uw0 * vw0 * sampleShadowTexels(tex, base, u0, v0, depth);
    sum += uw1 * vw0 * sampleShadowTexels(tex, base, u1, v0, depth);
    sum += uw2 * vw0 * sampleShadowTexels(tex, base, u2, v0, depth);
    sum += uw3 * vw0 * sampleShadowTexels(tex, base, u3, v0, depth);
    sum += uw0 * vw1 * sampleShadowTexels(tex, base, u0, v1, depth);
    sum += uw1 * vw1 * sampleShadowTexels(tex, base, u1, v1, depth);
    sum += uw2 * vw1 * sampleShadowTexels(tex, base, u2, v1, depth);
    sum += uw3 * vw1 * sampleShadowTexels(tex, base, u3, v1, depth);
    sum += uw0 * vw2 * sampleShadowTexels(tex, base, u0, v2, depth);
    sum += uw1 * vw2 * sampleShadowTexels(tex, base, u1, v2, depth);
    sum += uw2 * vw2 * sampleShadowTexels(tex, base, u2, v2, depth);
    sum += uw3 * vw2 * sampleShadowTexels(tex, base, u3, v2, depth);
    sum += uw0 * vw3 * sampleShadowTexels(tex, base, u0, v3, depth);
    sum += uw1 * vw3 * sampleShadowTexels(tex, base, u1, v3, depth);
    sum += uw2 * vw3 * sampleShadowTexels(tex, base, u2, v3, depth);
    sum += uw3 * vw3 * sampleShadowTexels(tex, base, u3, v3, depth);
    return sum / 2704.0;
}
#endif // @softShadows

#endif // SHADOWS

// ============================================================================

float unshadowedLightRatio(float distance)
{
    float shadowing = 1.0;
#if SHADOWS
#if @limitShadowMapDistance
    float fade = clamp((distance - shadowFadeStart) / (maximumShadowMapDistance - shadowFadeStart), 0.0, 1.0);
    if (fade == 1.0)
        return shadowing;
#endif
    bool doneShadows = false;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        if (!doneShadows)
        {
            vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
#if @perspectiveShadowMaps
            vec3 shadowRegionXYZ = shadowRegionCoords@shadow_texture_unit_index.xyz / shadowRegionCoords@shadow_texture_unit_index.w;
#endif
            // Reused maps can contain the pixel in XY but exclude its depth
            // or cascade region. Such maps must not darken a valid later map.
            bool validShadow = shadowSpaceCoords@shadow_texture_unit_index.w > 0.0
                && all(lessThan(shadowXYZ, vec3(1.0)))
                && all(greaterThan(shadowXYZ, vec3(0.0)));
#if @perspectiveShadowMaps
            validShadow = validShadow
                && all(lessThan(shadowRegionXYZ, vec3(1.0)))
                && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
            if (validShadow)
            {
#if @softShadows
                shadowing = min(getFilteredShadowing(
                    shadowTexture@shadow_texture_unit_index,
                    shadowSpaceCoords@shadow_texture_unit_index
                ), shadowing);
#else
                shadowing = min(shadow2DProj(shadowTexture@shadow_texture_unit_index, shadowSpaceCoords@shadow_texture_unit_index).r, shadowing);
#endif

                doneShadows = all(lessThan(shadowXYZ, vec3(0.95, 0.95, 1.0))) && all(greaterThan(shadowXYZ, vec3(0.05, 0.05, 0.0)));
#if @perspectiveShadowMaps
                doneShadows = doneShadows && all(lessThan(shadowRegionXYZ, vec3(1.0, 1.0, 1.0))) && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
            }
        }
    @endforeach
#if @limitShadowMapDistance
    shadowing = mix(shadowing, 1.0, fade);
#endif
#endif // SHADOWS
    return shadowing;
}

void applyShadowDebugOverlay()
{
#if SHADOWS && @useShadowDebugOverlay
    bool doneOverlay = false;
    float colourIndex = 0.0;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        if (!doneOverlay)
        {
            vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
#if @perspectiveShadowMaps
            vec3 shadowRegionXYZ = shadowRegionCoords@shadow_texture_unit_index.xyz / shadowRegionCoords@shadow_texture_unit_index.w;
#endif
            if (all(lessThan(shadowXYZ.xy, vec2(1.0, 1.0))) && all(greaterThan(shadowXYZ.xy, vec2(0.0, 0.0))))
            {
                colourIndex = mod(@shadow_texture_unit_index.0, 3.0);
                if (colourIndex < 1.0)
                    gl_FragData[0].x += 0.1;
                else if (colourIndex < 2.0)
                    gl_FragData[0].y += 0.1;
                else
                    gl_FragData[0].z += 0.1;

                doneOverlay = all(lessThan(shadowXYZ, vec3(0.95, 0.95, 1.0))) && all(greaterThan(shadowXYZ, vec3(0.05, 0.05, 0.0)));
#if @perspectiveShadowMaps
                doneOverlay = doneOverlay && all(lessThan(shadowRegionXYZ.xyz, vec3(1.0, 1.0, 1.0))) && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
            }
        }
    @endforeach
#endif // SHADOWS
}
