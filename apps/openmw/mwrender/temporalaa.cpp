#include "temporalaa.hpp"

#include <string>

#include <osg/Camera>
#include <osg/GLExtensions>
#include <osg/Program>
#include <osg/RenderInfo>
#include <osg/Shader>
#include <osg/State>

#include <components/settings/values.hpp>
#include <components/stereo/stereomanager.hpp>

#include "pingpongcanvas.hpp"

namespace MWRender
{
    namespace
    {
        // Texture units used by the resolve pass.
        constexpr int UnitScene = 0;
        constexpr int UnitHistory = 1;
        constexpr int UnitDepth = 2;
        constexpr int UnitPreviousDepth = 3;

        // Eight centred Halton (2, 3) samples, as in Infinite Cubes, scaled to
        // 0.6 of a pixel so edges settle without visibly softening.
        constexpr std::array<std::array<float, 2>, 8> Halton = { { { 0.5f, 1.0f / 3 }, { 0.25f, 2.0f / 3 },
            { 0.75f, 1.0f / 9 }, { 0.125f, 4.0f / 9 }, { 0.625f, 7.0f / 9 }, { 0.375f, 2.0f / 9 },
            { 0.875f, 5.0f / 9 }, { 0.0625f, 8.0f / 9 } } };
        constexpr float JitterScale = 0.6f;

        // A camera jump larger than this (teleport, cell change) drops the history.
        constexpr double CutDistance = 1500.0;

        const char* VertexSource = R"GLSL(#version 120
void main()
{
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

        // Adapted from Infinite Cubes' TemporalResolveShader (itself after
        // CWVulkan Steam's taa.frag). GLSL 1.20 so GL4ES can translate it to
        // GLES2 on Android; TAA_LOW trims the texture reads for phones.
        const char* FragmentSource = R"GLSL(
uniform sampler2D taaScene;
uniform sampler2D taaHistory;
uniform sampler2D taaDepth;
uniform sampler2D taaPrevDepth;
uniform mat4 taaClipToPrevious;
uniform vec4 taaMetrics;     // 1/width, 1/height, width, height
uniform vec4 taaJitter;      // current and previous jitter, in pixels
uniform vec4 taaTuning;      // history weight flat/edge, sharpen, use history
uniform vec2 taaDepthParams; // view distance = y / (ndc z + x)

float maxChannel(vec3 v) { return max(v.r, max(v.g, v.b)); }

float toNdc(float d)
{
#if TAA_REVERSED_Z
    return d;
#else
    return d * 2.0 - 1.0;
#endif
}

bool isSky(float d)
{
#if TAA_REVERSED_Z
    return d <= 0.000001;
#else
    return d >= 0.999999;
#endif
}

float viewDistance(float d) { return taaDepthParams.y / (toNdc(d) + taaDepthParams.x); }

vec3 historyAt(vec2 uv)
{
#if TAA_LOW
    return texture2D(taaHistory, uv).rgb;
#else
    // Catmull-Rom with five bilinear taps.
    vec2 position = uv * taaMetrics.zw;
    vec2 center = floor(position - 0.5) + 0.5;
    vec2 f = position - center;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (center - 1.0) * taaMetrics.xy;
    vec2 t3 = (center + 2.0) * taaMetrics.xy;
    vec2 t12 = (center + w2 / w12) * taaMetrics.xy;
    vec4 outer = vec4(w12.x * w0.y, w0.x * w12.y, w12.x * w3.y, w3.x * w12.y);
    float centerWeight = w12.x * w12.y;
    vec3 sum = texture2D(taaHistory, vec2(t12.x, t0.y)).rgb * outer.x
        + texture2D(taaHistory, vec2(t0.x, t12.y)).rgb * outer.y
        + texture2D(taaHistory, vec2(t12.x, t3.y)).rgb * outer.z
        + texture2D(taaHistory, vec2(t3.x, t12.y)).rgb * outer.w
        + texture2D(taaHistory, t12).rgb * centerWeight;
    return max(vec3(0.0), sum / (dot(outer, vec4(1.0)) + centerWeight));
#endif
}

bool sameSurfaceAt(vec2 texel, bool sky, float previousDistance)
{
    vec2 halfTexel = 0.5 * taaMetrics.xy;
    vec2 uv = clamp((texel + 0.5) * taaMetrics.xy, halfTexel, vec2(1.0) - halfTexel);
    float d = texture2D(taaPrevDepth, uv).r;
    if (sky)
        return isSky(d);
    return !isSky(d) && abs(viewDistance(d) - previousDistance) <= 0.02 * previousDistance + 4.0;
}

void main()
{
    vec2 outputUV = gl_FragCoord.xy * taaMetrics.xy;
    vec2 uv = outputUV + taaJitter.xy * taaMetrics.xy;
    vec4 current = texture2D(taaScene, uv);
    if (taaTuning.w < 0.5)
    {
        gl_FragColor = current;
        return;
    }

    vec3 lo = current.rgb;
    vec3 hi = current.rgb;
    vec3 axisSum = vec3(0.0);
#if TAA_LOW
    vec3 c1 = texture2D(taaScene, uv + vec2(taaMetrics.x, 0.0)).rgb;
    vec3 c2 = texture2D(taaScene, uv - vec2(taaMetrics.x, 0.0)).rgb;
    vec3 c3 = texture2D(taaScene, uv + vec2(0.0, taaMetrics.y)).rgb;
    vec3 c4 = texture2D(taaScene, uv - vec2(0.0, taaMetrics.y)).rgb;
    lo = min(lo, min(min(c1, c2), min(c3, c4)));
    hi = max(hi, max(max(c1, c2), max(c3, c4)));
    axisSum = c1 + c2 + c3 + c4;
#else
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec3 c = texture2D(taaScene, uv + vec2(float(x), float(y)) * taaMetrics.xy).rgb;
            lo = min(lo, c);
            hi = max(hi, c);
            if (x * x + y * y == 1)
                axisSum += c;
        }
    }
#endif

    // Reproject: the depth at this pixel was rendered with this frame's jitter.
    float depth = texture2D(taaDepth, outputUV).r;
    vec2 sourceUV = outputUV - taaJitter.xy * taaMetrics.xy;
    vec4 previous = taaClipToPrevious * vec4(sourceUV * 2.0 - 1.0, toNdc(depth), 1.0);
    vec2 previousUV = previous.xy / max(previous.w, 0.000001) * 0.5 + 0.5;
    vec2 motion = previousUV - sourceUV;
    vec2 historyUV = outputUV + motion;
    // The previous depth buffer was rendered with the previous jitter.
    vec2 depthUV = previousUV + taaJitter.zw * taaMetrics.xy;
    bool sky = isSky(depth);
    bool inside = previous.w > 0.0 && all(greaterThanEqual(historyUV, vec2(0.0)))
        && all(lessThanEqual(historyUV, vec2(1.0))) && all(greaterThanEqual(depthUV, vec2(0.0)))
        && all(lessThanEqual(depthUV, vec2(1.0)));

    // previous is homogeneous (scaled by 1 / current w), so take the distance
    // from its depth rather than from its w.
    float previousDistance = taaDepthParams.y / (previous.z / max(previous.w, 0.000001) + taaDepthParams.x);
    bool sameSurface = false;
#if TAA_LOW
    sameSurface = sameSurfaceAt(floor(depthUV * taaMetrics.zw), sky, previousDistance);
#else
    vec2 depthBase = floor(depthUV * taaMetrics.zw - 0.5);
    sameSurface = sameSurfaceAt(depthBase, sky, previousDistance)
        || sameSurfaceAt(depthBase + vec2(1.0, 0.0), sky, previousDistance)
        || sameSurfaceAt(depthBase + vec2(0.0, 1.0), sky, previousDistance)
        || sameSurfaceAt(depthBase + vec2(1.0, 1.0), sky, previousDistance);
#endif

    vec3 raw = historyAt(clamp(historyUV, vec2(0.0), vec2(1.0)));

    // Clip the history toward the current sample within the local colour range.
    vec3 delta = raw - current.rgb;
    vec3 room = vec3(delta.x > 0.0 ? hi.x - current.r : current.r - lo.x,
        delta.y > 0.0 ? hi.y - current.g : current.g - lo.y, delta.z > 0.0 ? hi.z - current.b : current.b - lo.z);
    vec3 limits = clamp((room + 0.00001) / max(abs(delta), vec3(0.00001)), 0.0, 1.0);
    vec3 clipped = current.rgb + delta * min(limits.x, min(limits.y, limits.z));
    float confidence = 1.0 - smoothstep(0.02, 0.12, maxChannel(abs(raw - clipped)));
    float edge = smoothstep(0.04, 0.20, maxChannel(hi - lo));
    float still = 1.0 - smoothstep(0.05, 1.0, length(motion * taaMetrics.zw));
    float historyWeightAtEdge = mix(taaTuning.x, taaTuning.y, edge);
    float accumulation = (inside && sameSurface) ? mix(historyWeightAtEdge, 0.94, still) * confidence : 0.0;
    vec3 filtered = mix(current.rgb, (current.rgb * 4.0 + axisSum) / 8.0, taaTuning.z);
    float currentWeight = (1.0 - accumulation) / (1.0 + maxChannel(filtered));
    float historyWeight = accumulation / (1.0 + maxChannel(clipped));
    gl_FragColor = vec4((filtered * currentWeight + clipped * historyWeight) / (currentWeight + historyWeight),
        current.a);
}
)GLSL";

        osg::ref_ptr<osg::Program> createProgram(bool reversedZ, bool low)
        {
            std::string fragment = "#version 120\n";
            fragment += std::string("#define TAA_REVERSED_Z ") + (reversedZ ? "1" : "0") + "\n";
            fragment += std::string("#define TAA_LOW ") + (low ? "1" : "0") + "\n";
            fragment += FragmentSource;

            osg::ref_ptr<osg::Program> program = new osg::Program;
            program->addShader(new osg::Shader(osg::Shader::VERTEX, VertexSource));
            program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragment));
            return program;
        }
    }

    TemporalAA::TemporalAA(bool reversedZ)
    {
#ifdef ANDROID
        constexpr bool low = true;
#else
        constexpr bool low = false;
#endif
        osg::ref_ptr<osg::Program> program = createProgram(reversedZ, low);
        for (Frame& frame : mFrames)
        {
            frame.mStateSet = new osg::StateSet;
            frame.mStateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
            frame.mStateSet->addUniform(new osg::Uniform("taaScene", UnitScene));
            frame.mStateSet->addUniform(new osg::Uniform("taaHistory", UnitHistory));
            frame.mStateSet->addUniform(new osg::Uniform("taaDepth", UnitDepth));
            frame.mStateSet->addUniform(new osg::Uniform("taaPrevDepth", UnitPreviousDepth));
            frame.mClipToPrevious = new osg::Uniform("taaClipToPrevious", osg::Matrixf());
            frame.mMetrics = new osg::Uniform("taaMetrics", osg::Vec4f(1, 1, 1, 1));
            frame.mJitter = new osg::Uniform("taaJitter", osg::Vec4f());
            frame.mTuning = new osg::Uniform("taaTuning", osg::Vec4f());
            frame.mDepthParams = new osg::Uniform("taaDepthParams", osg::Vec2f(0, 1));
            for (osg::Uniform* uniform :
                { frame.mClipToPrevious.get(), frame.mMetrics.get(), frame.mJitter.get(), frame.mTuning.get(),
                    frame.mDepthParams.get() })
                frame.mStateSet->addUniform(uniform);
            frame.mViewport = new osg::Viewport;
            frame.mStateSet->setAttribute(frame.mViewport);
            frame.mStateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            frame.mStateSet->setMode(GL_BLEND, osg::StateAttribute::OFF);
            frame.mStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        }
    }

    bool TemporalAA::isEnabled() const
    {
        return Settings::postProcessing().mTemporalAntialiasing && !Stereo::getStereo();
    }

    osg::Vec2f TemporalAA::advanceJitter()
    {
        if (!isEnabled())
        {
            mPendingJitter = osg::Vec2f();
            return mPendingJitter;
        }
        const auto& sample = Halton[mJitterIndex++ % Halton.size()];
        mPendingJitter = osg::Vec2f((sample[0] - 0.5f) * JitterScale, (sample[1] - 0.5f) * JitterScale);
        return mPendingJitter;
    }

    void TemporalAA::cull(unsigned frameId, const osg::Matrixd& view, const osg::Vec3d& eye, osg::Texture* depth,
        osg::Texture* previousDepth, int width, int height)
    {
        Frame& frame = mFrames[frameId % 2];
        frame.mActive = isEnabled() && depth && previousDepth && width > 0 && height > 0;
        if (!frame.mActive)
        {
            mPreviousValid = false;
            return;
        }

        const osg::Matrixd clip = view * mProjection;
        const bool cut = (eye - mPreviousEye).length() > CutDistance || width != mPreviousWidth
            || height != mPreviousHeight || mProjection != mPreviousProjection;
        frame.mReproject = mPreviousValid && !cut;
        if (frame.mReproject)
        {
            // Current clip space -> world -> previous (unjittered) clip space.
            frame.mClipToPrevious->set(osg::Matrixf(osg::Matrixd::inverse(clip) * mPreviousClip));
        }
        frame.mMetrics->set(osg::Vec4f(1.f / width, 1.f / height, float(width), float(height)));
        frame.mJitter->set(
            osg::Vec4f(mPendingJitter.x(), mPendingJitter.y(), mPreviousJitter.x(), mPreviousJitter.y()));
        // osg matrices are row-vector: clip.z = z * P(2,2) + P(3,2), clip.w = -z.
        frame.mDepthParams->set(osg::Vec2f(float(mProjection(2, 2)), float(mProjection(3, 2))));
        frame.mViewport->setViewport(0, 0, width, height);
        frame.mDepth = depth;
        frame.mPreviousDepth = previousDepth;

        mPreviousClip = clip;
        mPreviousProjection = mProjection;
        mPreviousJitter = mPendingJitter;
        mPreviousEye = eye;
        mPreviousWidth = width;
        mPreviousHeight = height;
        mPreviousValid = true;
    }

    osg::Texture* TemporalAA::resolve(osg::RenderInfo& renderInfo, const PingPongCanvas& canvas, osg::Texture* scene,
        unsigned frameId, unsigned frameNumber) const
    {
        const Frame& frame = mFrames[frameId % 2];
        auto* scene2D = dynamic_cast<osg::Texture2D*>(scene);
        if (!frame.mActive || !scene2D || !frame.mDepth || !frame.mPreviousDepth)
        {
            mHaveHistory = false;
            return scene;
        }

        const int width = scene2D->getTextureWidth();
        const int height = scene2D->getTextureHeight();
        if (width <= 0 || height <= 0)
            return scene;

        osg::State& state = *renderInfo.getState();

        if (!mHistory[0] || width != mHistoryWidth || height != mHistoryHeight)
        {
            for (size_t i = 0; i < mHistory.size(); ++i)
            {
                osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
                texture->setTextureSize(width, height);
                texture->setSourceFormat(scene2D->getSourceFormat());
                texture->setSourceType(scene2D->getSourceType());
                texture->setInternalFormat(scene2D->getInternalFormat());
                texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
                texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
                texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
                texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
                texture->setResizeNonPowerOfTwoHint(false);
                mHistory[i] = texture;
                mHistoryFbo[i] = new osg::FrameBufferObject;
                mHistoryFbo[i]->setAttachment(osg::Camera::COLOR_BUFFER0, osg::FrameBufferAttachment(texture));
            }
            mHistoryWidth = width;
            mHistoryHeight = height;
            mHaveHistory = false;
        }

        if (mResetRequested)
        {
            mResetRequested = false;
            mHaveHistory = false;
        }

        const int write = mWriteIndex;
        const int read = 1 - write;
        const bool useHistory = mHaveHistory && frame.mReproject && frameNumber == mLastResolvedFrame + 1;
        frame.mTuning->set(osg::Vec4f(0.72f, 0.86f, 0.08f, useHistory ? 1.f : 0.f));

        frame.mStateSet->setTextureAttribute(UnitScene, scene);
        frame.mStateSet->setTextureAttribute(UnitHistory, useHistory ? mHistory[read].get() : scene);
        frame.mStateSet->setTextureAttribute(UnitDepth, frame.mDepth);
        frame.mStateSet->setTextureAttribute(UnitPreviousDepth, frame.mPreviousDepth);

        mHistoryFbo[write]->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
        state.pushStateSet(frame.mStateSet);
        state.apply();
        canvas.drawGeometry(renderInfo);
        state.popStateSet();
        state.apply();

        mWriteIndex = read;
        mHaveHistory = true;
        mLastResolvedFrame = frameNumber;
        return mHistory[write].get();
    }
}
