#ifndef LIB_WATER_RIPPLES
#define LIB_WATER_RIPPLES

float getTemporalWaveSizeMultiplier(in float time)
{
    return 1.0 + 0.055 * sin(16.0 * time) + 0.065 * sin(12.87645 * time);
}

vec4 applySprings(in vec4 samplerData, in vec4 n, in vec4 n2)
{
    vec4 storage = vec4(0.0, samplerData.r, 0.0, 0.0);

    // Tweak to look most like water, not a physically accurate simulation
    const float a = 0.28;
    const float udamp = 0.04;
    const float vdamp = 0.04;

    // Apply 2d wave equation with dampening
    // Continous impulse needed to maintain simulation, otherwise ripples will fade
    float nsum = n.x + n.y + n.z + n.w;
    storage.r = a * nsum + ((2.0 - udamp - vdamp) - 4.0 * a) * samplerData.r - (1.0 - vdamp) * samplerData.g;

    // Calculate normal and store in blue-alpha channel
    storage.ba = 2.0 * (n.xz - n.yw) + 0.5 * (n2.xz - n2.yw);

    return storage;
}

#if @rippleStatePacked
// OPENMW_ANDROID_053_PACKED_RIPPLES
// Phones without half-float render targets give GL4ES an 8-bit texture for
// the ripple state, which cannot hold the negative wave heights, so no ripple
// ever showed. Each height is stored instead as a signed 16-bit value in two
// 8-bit channels: current height in rg, previous height in ba. All-zero bytes
// are still water, so a fresh texture is calm.
const float RIPPLE_HEIGHT_SCALE = 8192.0; // heights from -4 to 4

vec2 packRippleHeight(float height)
{
    float value = floor(clamp(height * RIPPLE_HEIGHT_SCALE, -32767.0, 32767.0) + 0.5);
    if (value < 0.0)
        value += 65536.0;
    float high = floor(value / 256.0);
    return vec2(high, value - high * 256.0) / 255.0;
}

float unpackRippleHeight(vec2 stored)
{
    vec2 bytes = floor(stored * 255.0 + vec2(0.5));
    float value = bytes.x * 256.0 + bytes.y;
    if (value >= 32768.0)
        value -= 65536.0;
    return value / RIPPLE_HEIGHT_SCALE;
}

vec4 packRippleState(vec2 heights)
{
    return vec4(packRippleHeight(heights.x), packRippleHeight(heights.y));
}

// Outside the map the water is still, as with the desktop's zero border.
vec2 sampleRippleState(sampler2D state, vec2 uv)
{
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return vec2(0.0);
    vec4 bytesIn = texture2D(state, uv);
    return vec2(unpackRippleHeight(bytesIn.rg), unpackRippleHeight(bytesIn.ba));
}

float sampleRippleHeight(sampler2D state, vec2 uv)
{
    return sampleRippleState(state, uv).x;
}
#endif

#endif
