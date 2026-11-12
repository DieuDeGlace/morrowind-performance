#version 120

// OPENMW_ANDROID_053_PACKED_RIPPLES
// Turns the ripple heights into the surface slope the water shader reads,
// in a filtered 8-bit texture. 8 bits spread evenly over the slope range are
// far too coarse for gentle ripples, so blue-alpha store the square root of
// the slope's size (with its sign) around 128/255, which 8 bits represent
// exactly, and red marks written texels so a texture not drawn yet reads as
// calm water. water.frag reverses this.

uniform sampler2D imageIn;

#include "lib/water/ripples.glsl"

const float RIPPLE_SLOPE_RANGE = 4.0;

void main()
{
    vec2 uv = gl_FragCoord.xy / @rippleMapSize;
    float pixelSize = 1.0 / @rippleMapSize;

    vec4 n = vec4(
        sampleRippleHeight(imageIn, uv + vec2(pixelSize, 0.0)),
        sampleRippleHeight(imageIn, uv + vec2(-pixelSize, 0.0)),
        sampleRippleHeight(imageIn, uv + vec2(0.0, pixelSize)),
        sampleRippleHeight(imageIn, uv + vec2(0.0, -pixelSize))
    );

    // The desktop slope also reads 1.5 texels out with linear filtering, which
    // on unfiltered texels is the mean of the samples 1 and 2 texels out.
    vec4 far = vec4(
        sampleRippleHeight(imageIn, uv + vec2(2.0 * pixelSize, 0.0)),
        sampleRippleHeight(imageIn, uv + vec2(-2.0 * pixelSize, 0.0)),
        sampleRippleHeight(imageIn, uv + vec2(0.0, 2.0 * pixelSize)),
        sampleRippleHeight(imageIn, uv + vec2(0.0, -2.0 * pixelSize))
    );
    vec4 n2 = 0.5 * (n + far);
    vec2 slope = 2.0 * (n.xz - n.yw) + 0.5 * (n2.xz - n2.yw);
    vec2 companded = sign(slope) * sqrt(min(abs(slope) / RIPPLE_SLOPE_RANGE, vec2(1.0)));
    gl_FragColor = vec4(1.0, 0.0, (vec2(128.0) + companded * 127.0) / 255.0);
}
