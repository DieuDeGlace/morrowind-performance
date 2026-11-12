#version 120

uniform sampler2D imageIn;

#define MAX_POSITIONS 100
uniform vec3 positions[MAX_POSITIONS];
uniform int positionCount;

uniform float osg_SimulationTime;
uniform vec2 offset;

#include "lib/water/ripples.glsl"

void main()
{
    vec2 uv = (gl_FragCoord.xy + offset) / @rippleMapSize;

#if @rippleStatePacked
    vec2 heights = sampleRippleState(imageIn, uv);
#else
    vec4 color = texture2D(imageIn, uv);
#endif
    float wavesizeMultiplier = getTemporalWaveSizeMultiplier(osg_SimulationTime);
    // GLSL ES 1.00 only guarantees loops with a constant bound.
    for (int i = 0; i < MAX_POSITIONS; ++i)
    {
        if (i >= positionCount)
            break;
        float wavesize = wavesizeMultiplier * positions[i].z;
        float displace = clamp(0.2 * abs(length((positions[i].xy + offset) - gl_FragCoord.xy) / wavesize - 1.0) + 0.8, 0.0, 1.0);
#if @rippleStatePacked
        heights = mix(vec2(-1.0), heights, displace);
#else
        color.rg = mix(vec2(-1.0), color.rg, displace);
#endif
    }

#if @rippleStatePacked
    gl_FragColor = packRippleState(heights);
#else
    gl_FragColor = color;
#endif
}
