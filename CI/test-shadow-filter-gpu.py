"""Check the shipped soft-shadow GLSL against an independent plane oracle.

Requires moderngl and numpy. Linux uses EGL (Mesa works without a display).
An optional old shader proves that the regression fixture detects the defect.
"""

import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import re
import sys

import moderngl
import numpy as np


VERTEX = """#version 330
in vec2 position;
void main() { gl_Position = vec4(position, 0.0, 1.0); }
"""


def plane_error(ctx, source, perspective):
    # Execute the production direction calculation, with only the fixed-function
    # normal-matrix input replaced by a uniform for a standalone GL context.
    start = source.index("    // ---- Compute filter directions")
    end = source.index("    // Build offset vectors", start)
    block = source[start:end].replace("gl_NormalMatrix", "normalMatrix")
    fragment = """#version 330
#define PER_PIXEL_LIGHTING 1
uniform mat4 matrix;
uniform mat3 normalMatrix;
uniform vec3 passNormal;
uniform vec4 coord;
uniform vec3 clipNormal;
out vec4 result;
void main() {
    vec3 shadowUV = coord.xyz / coord.w;
""" + block + """
    result = vec4(abs(dot(clipNormal, filterX)),
                  abs(dot(clipNormal, filterY)), 0.0, 1.0);
}
"""
    plane = np.array([-.4, .2, 1, 0], dtype="f4")
    projection = np.array([
        [.04, .015, 0, .5], [0, .04, .012, .5],
        [.005, .003, .015, .5], [0, .035, .01, 1],
    ], dtype="f4")
    if not perspective:
        projection[3] = [0, 0, 0, 1]
    # A plane transforms by inverse transpose. The filter axes must remain in
    # that plane after projection, independently of the camera's view rotation.
    projected_plane = np.linalg.inv(projection).T @ plane
    with ExitStack() as resources:
        program = ctx.program(vertex_shader=VERTEX, fragment_shader=fragment)
        resources.callback(program.release)
        program["passNormal"].value = tuple(plane[:3] / np.linalg.norm(plane[:3]))
        program["coord"].value = tuple(projection @ np.array([1, 2, 0, 1], dtype="f4"))
        program["clipNormal"].value = tuple(projected_plane[:3] / np.linalg.norm(projected_plane[:3]))
        vbo = ctx.buffer(np.array([-1, -1, 3, -1, -1, 3], dtype="f4").tobytes())
        resources.callback(vbo.release)
        vao = ctx.vertex_array(program, [(vbo, "2f", "position")])
        resources.callback(vao.release)
        errors = []
        for angle in np.linspace(-1.4, 1.4, 57):
            c, s = np.cos(angle), np.sin(angle)
            view = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype="f4")
            inverse_view = np.eye(4, dtype="f4")
            inverse_view[:3, :3] = view.T
            program["matrix"].write((projection @ inverse_view).T.tobytes())
            program["normalMatrix"].write(view.T.tobytes())
            vao.render(moderngl.TRIANGLES)
            values = np.frombuffer(ctx.fbo.read(components=4, dtype="f4"), dtype="f4")
            assert np.isfinite(values).all(), "Non-finite filter axis"
            errors.append(float(values[:2].max()))
    assert ctx.error == "GL_NO_ERROR"
    return {"views": len(errors), "max_normalized_plane_error": max(errors)}


def compile_filter(ctx, source, per_pixel, soft):
    source = re.sub(
        r"@foreach shadow_texture_unit_index @shadow_texture_unit_list\s*(.*?)@endforeach",
        lambda match: match[1].replace("@shadow_texture_unit_index", "0"), source, flags=re.S,
    )
    for key, value in {
        "shadows_enabled": 1, "softShadows": int(soft), "shadowMapResolution": 4096,
        "softShadowRadius": 1.5, "perspectiveShadowMaps": 1,
        "limitShadowMapDistance": 1, "useShadowDebugOverlay": 0,
    }.items():
        source = source.replace("@" + key, str(value))
    assert "@" not in source, "Unexpanded shader template"
    source = source.replace("varying ", "in ").replace("gl_NormalMatrix", "normalMatrix")
    source = source.replace("gl_FragData[0]", "result")
    fragment = """#version 330
#define shadow2DProj(s, c) vec4(textureProj(s, c))
uniform mat3 normalMatrix;
uniform vec3 passNormal;
out vec4 result;
""" + f"#define PER_PIXEL_LIGHTING {int(per_pixel)}\n" + source + """
void main() { result = vec4(vec3(unshadowedLightRatio(100.0)), 1.0); }
"""
    vertex = """#version 330
in vec2 position;
out vec4 shadowSpaceCoords0;
out vec4 shadowRegionCoords0;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    shadowSpaceCoords0 = vec4(position * 0.25 + 0.5, 0.5, 1.0);
    shadowRegionCoords0 = vec4(position * 0.5, 0.5, 1.0);
}
"""
    program = ctx.program(vertex_shader=vertex, fragment_shader=fragment)
    program.release()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shader", type=Path, default=Path(__file__).resolve().parents[1] / "files/shaders/compatibility/shadows_fragment.glsl")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    ctx = moderngl.create_standalone_context(require=330, **({"backend": "egl"} if sys.platform.startswith("linux") else {}))
    fbo = ctx.simple_framebuffer((1, 1), dtype="f4")
    fbo.use()
    source = args.shader.read_text(encoding="utf-8")
    results = {name: plane_error(ctx, source, perspective) for name, perspective in (("orthographic", False), ("perspective", True))}
    for name, result in results.items():
        assert result["max_normalized_plane_error"] < 1e-5, (name, result)
    for per_pixel in (False, True):
        for soft in (False, True):
            compile_filter(ctx, source, per_pixel, soft)
    if args.baseline:
        results["baseline_perspective"] = plane_error(ctx, args.baseline.read_text(encoding="utf-8"), True)
        assert results["baseline_perspective"]["max_normalized_plane_error"] > .01, "Baseline no longer exercises the regression"
    report = {"result": "passed", "renderer": ctx.info["GL_RENDERER"], "plane_oracle": results, "shader_variants_compiled": 4}
    rendered = json.dumps(report, indent=2) + "\n"
    print(rendered, end="")
    if args.report:
        args.report.write_text(rendered, encoding="utf-8")
    fbo.release()
    ctx.release()


if __name__ == "__main__":
    main()
