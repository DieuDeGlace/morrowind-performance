#version 120

uniform sampler2D imageIn;

#include "lib/water/ripples.glsl"

void main()
{
    vec2 uv = gl_FragCoord.xy / @rippleMapSize;

    float pixelSize = 1.0 / @rippleMapSize;

    float oneOffset = pixelSize;
    float oneAndHalfOffset = 1.5 * pixelSize;

#if @rippleStatePacked
    // The normals are made by a separate pass (ripples_normals.frag), so the
    // wave step needs only its four neighbours.
    vec4 n = vec4(
        sampleRippleHeight(imageIn, uv + vec2(oneOffset, 0.0)),
        sampleRippleHeight(imageIn, uv + vec2(-oneOffset, 0.0)),
        sampleRippleHeight(imageIn, uv + vec2(0.0, oneOffset)),
        sampleRippleHeight(imageIn, uv + vec2(0.0, -oneOffset))
    );
    vec2 heights = sampleRippleState(imageIn, uv);
    vec4 storage = applySprings(vec4(heights, 0.0, 0.0), n, vec4(0.0));
    gl_FragColor = packRippleState(storage.rg);
#else
    vec4 n = vec4(
        texture2D(imageIn, uv + vec2(oneOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(-oneOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(0.0, oneOffset)).r,
        texture2D(imageIn, uv + vec2(0.0, -oneOffset)).r
    );

    vec4 n2 = vec4(
        texture2D(imageIn, uv + vec2(oneAndHalfOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(-oneAndHalfOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(0.0, oneAndHalfOffset)).r,
        texture2D(imageIn, uv + vec2(0.0, -oneAndHalfOffset)).r
    );

    vec4 color = texture2D(imageIn, uv);

    gl_FragColor = applySprings(color, n, n2);
#endif
}
