"""Check the shipped soft-shadow GLSL on a GPU.

Requires moderngl and numpy. Linux uses EGL (Mesa works without a display).
An optional old shader proves that the regression fixtures detect the defects.
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

# Desktop GLSL 1.20 shadow lookups, expressed for a 3.30 test context.
SHADOW_LOOKUPS = """#define shadow2DProj(s, c) vec4(textureProj(s, c))
#define shadow2D(s, c) vec4(texture(s, c))
"""

FLICKER_RESOLUTION = 256
FLICKER_PIXELS = 512
FLICKER_SPAN = 64.0  # shadow-map texels across the row of receivers
# Worst change of the shadow at a fixed point when the map slides an eighth
# of a texel, per soft shadow radius. A one-texel leaf still jumps a whole
# texel in the map whenever the grid crosses it, which a filter of this width
# cannot hide completely; the released 8-tap spiral changed by 0.29-0.46.
MAX_FLICKER = {0.5: 0.25, 1.0: 0.17, 1.5: 0.14, 3.0: 0.14}


def expand_single_map(source, soft, radius, resolution):
    source = re.sub(
        r"@foreach shadow_texture_unit_index @shadow_texture_unit_list\s*(.*?)@endforeach",
        lambda match: match[1].replace("@shadow_texture_unit_index", "0"), source, flags=re.S,
    )
    for key, value in {
        "shadows_enabled": 1, "softShadows": int(soft), "shadowMapResolution": f"{resolution}.0",
        "softShadowRadius": radius, "perspectiveShadowMaps": 0,
        "limitShadowMapDistance": 0, "useShadowDebugOverlay": 0,
    }.items():
        source = source.replace("@" + key, str(value))
    assert "@" not in source, "Unexpanded shader template"
    return source.replace("varying ", "in ").replace("gl_NormalMatrix", "normalMatrix").replace("gl_FragData[0]", "result")


def moving_map_flicker(ctx, source, radius):
    """Thin casters stay still in the world while the shadow map's texel grid
    slides under them by a fraction of a texel, as it does each frame when
    the camera moves. Returns how much the shadow at a fixed world point
    changes between successive eighth-of-a-texel slides."""
    fragment = "#version 330\n#define PER_PIXEL_LIGHTING 0\n" + SHADOW_LOOKUPS + """uniform mat3 normalMatrix;
uniform vec3 passNormal;
out vec4 result;
""" + expand_single_map(source, True, radius, FLICKER_RESOLUTION) + """
void main() { result = vec4(vec3(unshadowedLightRatio(100.0)), 1.0); }
"""
    vertex = f"""#version 330
in vec2 position;
uniform float gridShift;
out vec4 shadowSpaceCoords0;
void main() {{
    gl_Position = vec4(position, 0.0, 1.0);
    // World x in shadow-map texels, from 0 to the span across the row.
    float world = (position.x * 0.5 + 0.5) * {FLICKER_SPAN};
    shadowSpaceCoords0 = vec4((world + 32.0 + gridShift) / {FLICKER_RESOLUTION}.0, 0.5 + position.y * 0.05, 0.5, 1.0);
}}
"""
    res = FLICKER_RESOLUTION
    # One-texel-wide leaves every few texels, at irregular spacing.
    leaves = np.cumsum(np.tile([5.0, 7.0, 4.0, 9.0, 6.0], 12)) + 30.0
    with ExitStack() as resources:
        fbo = ctx.simple_framebuffer((FLICKER_PIXELS, 1), components=1, dtype="f4")
        resources.callback(fbo.release)
        fbo.use()
        program = ctx.program(vertex_shader=vertex, fragment_shader=fragment)
        resources.callback(program.release)
        for name, value in (("normalMatrix", None), ("passNormal", (0, 0, 1))):
            if name in program:
                if value is None:
                    program[name].write(np.eye(3, dtype="f4").tobytes())
                else:
                    program[name].value = value
        if "shadowSpaceMatrix0" in program:
            program["shadowSpaceMatrix0"].write(np.eye(4, dtype="f4").tobytes())
        program["shadowTexture0"].value = 0
        vbo = ctx.buffer(np.array([-1, -1, 3, -1, -1, 3], dtype="f4").tobytes())
        resources.callback(vbo.release)
        vao = ctx.vertex_array(program, [(vbo, "2f", "position")])
        resources.callback(vao.release)
        rows = []
        for step in range(17):
            shift = step / 8.0
            # Rasterize the leaves into the shifted grid: a texel holds a
            # leaf when its centre lies inside it.
            centres = np.arange(res) + 0.5 - 32.0 - shift
            covered = np.zeros(res, dtype=bool)
            for leaf in leaves:
                covered |= (centres >= leaf) & (centres < leaf + 1.0)
            column = np.where(covered, 0.25, 1.0).astype("f4")
            texture = ctx.depth_texture((res, res), np.broadcast_to(column, (res, res)).copy().tobytes())
            texture.compare_func = "<="
            texture.filter = (moderngl.LINEAR, moderngl.LINEAR)
            texture.repeat_x = texture.repeat_y = False
            texture.use(0)
            program["gridShift"].value = shift
            fbo.clear()
            vao.render(moderngl.TRIANGLES)
            row = np.frombuffer(fbo.read(components=1, dtype="f4"), dtype="f4").copy()
            texture.release()
            assert np.isfinite(row).all(), "Non-finite shadow"
            rows.append(row)
    assert ctx.error == "GL_NO_ERROR"
    rows = np.array(rows)
    # Ignore the row ends, where the filter reaches past the leaves.
    rows = rows[:, FLICKER_PIXELS // 8: -FLICKER_PIXELS // 8]
    change = np.abs(np.diff(rows, axis=0))
    return {"radius": radius, "slides": int(change.shape[0]),
            "max_change_per_eighth_texel": float(change.max()),
            "mean_change_per_eighth_texel": float(change.mean()),
            "darkest": float(rows.min())}

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
""" + SHADOW_LOOKUPS + """uniform mat3 normalMatrix;
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


def shadow_volume_error(ctx, source, map_count, perspective, soft):
    """An invalid near cascade must never shadow a valid, lit far cascade."""
    source = re.sub(
        r"@foreach shadow_texture_unit_index @shadow_texture_unit_list\s*(.*?)@endforeach",
        lambda match: "\n".join(match[1].replace("@shadow_texture_unit_index", str(i))
                                for i in range(map_count)), source, flags=re.S,
    )
    for key, value in {
        "shadows_enabled": 1, "softShadows": int(soft), "shadowMapResolution": 4096,
        "softShadowRadius": 1.5, "perspectiveShadowMaps": int(perspective),
        "limitShadowMapDistance": 0, "useShadowDebugOverlay": 0,
    }.items():
        source = source.replace("@" + key, str(value))
    assert "@" not in source, "Unexpanded shader template"
    source = source.replace("varying ", "in ").replace("gl_NormalMatrix", "normalMatrix")
    source = source.replace("gl_FragData[0]", "result")
    fragment = """#version 330
#define PER_PIXEL_LIGHTING 1
""" + SHADOW_LOOKUPS + """uniform mat3 normalMatrix;
uniform vec3 passNormal;
out vec4 result;
""" + source + "\nvoid main(){result=vec4(vec3(unshadowedLightRatio(0.0)),1.0);}\n"
    vertex = "#version 330\nin vec2 position;\n"
    for i in range(map_count):
        vertex += f"""uniform vec4 coordinates{i};
uniform vec4 region{i};
out vec4 shadowSpaceCoords{i};
out vec4 shadowRegionCoords{i};
"""
    vertex += "void main(){gl_Position=vec4(position,0.0,1.0);\n"
    for i in range(map_count):
        vertex += f"shadowSpaceCoords{i}=coordinates{i};shadowRegionCoords{i}=region{i};\n"
    vertex += "}\n"
    with ExitStack() as resources:
        program = ctx.program(vertex_shader=vertex, fragment_shader=fragment)
        resources.callback(program.release)
        vbo = ctx.buffer(np.array([-1, -1, 3, -1, -1, 3], dtype="f4").tobytes())
        resources.callback(vbo.release)
        vao = ctx.vertex_array(program, [(vbo, "2f", "position")])
        resources.callback(vao.release)
        if "normalMatrix" in program:
            program["normalMatrix"].write(np.eye(3, dtype="f4").tobytes())
        if "passNormal" in program:
            program["passNormal"].value = (0, 0, 1)
        for i in range(map_count):
            # Invalid near maps contain an occluder. The far map is clear.
            depth = .25 if i + 1 < map_count else 1.0
            texture = ctx.depth_texture((4, 4), np.full((4, 4), depth, dtype="f4").tobytes())
            resources.callback(texture.release)
            texture.compare_func = "<="
            texture.filter = (moderngl.NEAREST, moderngl.NEAREST)
            texture.use(i)
            program[f"shadowTexture{i}"].value = i
            name = f"shadowSpaceMatrix{i}"
            if name in program:
                program[name].write(np.eye(4, dtype="f4").tobytes())

        cases = {
            "beyond_depth": ((.5, .5, 1.2, 1), (0, 0, .5, 1)),
            "before_depth": ((.5, .5, -.2, 1), (0, 0, .5, 1)),
            "behind_projection": ((-.5, -.5, -.5, -1), (0, 0, .5, 1)),
            "outside_uv": ((1.2, .5, .5, 1), (0, 0, .5, 1)),
        }
        if perspective:
            cases["outside_region"] = ((.5, .5, .5, 1), (1.2, 0, .5, 1))
            cases["beyond_region_depth"] = ((.5, .5, .5, 1), (0, 0, 1.2, 1))
        errors = {}
        for name, (coordinates, region) in cases.items():
            for i in range(map_count):
                last_valid = i + 1 == map_count and map_count > 1
                program[f"coordinates{i}"].value = (.5, .5, .5, 1) if last_valid else coordinates
                key = f"region{i}"
                if key in program:
                    program[key].value = (0, 0, .5, 1) if last_valid else region
            vao.render(moderngl.TRIANGLES)
            actual = float(np.frombuffer(ctx.fbo.read(components=1, dtype="f4"), dtype="f4")[0])
            errors[name] = abs(actual - 1.0)

        # A valid occluder in a near map must still win over the clear far map.
        if map_count > 1:
            program["coordinates0"].value = (.5, .5, .5, 1)
            if "region0" in program:
                program["region0"].value = (0, 0, .5, 1)
            vao.render(moderngl.TRIANGLES)
            errors["valid_near_shadow"] = abs(float(np.frombuffer(
                ctx.fbo.read(components=1, dtype="f4"), dtype="f4")[0]))
    assert ctx.error == "GL_NO_ERROR"
    return {"maps": map_count, "perspective": perspective, "soft": soft,
            "cases": len(errors), "max_visibility_error": max(errors.values()),
            "failures": [name for name, error in errors.items() if error > 1e-5]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shader", type=Path, default=Path(__file__).resolve().parents[1] / "files/shaders/compatibility/shadows_fragment.glsl")
    parser.add_argument("--baseline", type=Path, help="Old shader used as a moving-map flicker negative control")
    parser.add_argument("--volume-baseline", type=Path, help="Old shader used as an invalid-cascade negative control")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    ctx = moderngl.create_standalone_context(require=330, **({"backend": "egl"} if sys.platform.startswith("linux") else {}))
    fbo = ctx.simple_framebuffer((1, 1), dtype="f4")
    fbo.use()
    source = args.shader.read_text(encoding="utf-8")
    results = {}
    flicker = [moving_map_flicker(ctx, source, radius) for radius in (0.5, 1.0, 1.5, 3.0)]
    for result in flicker:
        # Leaves still cast a shadow, and a slide of an eighth of a texel
        # only moves it by a small, smooth amount.
        assert result["darkest"] < .97, result
        assert result["max_change_per_eighth_texel"] < MAX_FLICKER[result["radius"]], result
    results["moving_map"] = flicker
    fbo.use()
    for per_pixel in (False, True):
        for soft in (False, True):
            compile_filter(ctx, source, per_pixel, soft)
    volume = [shadow_volume_error(ctx, source, maps, perspective, soft)
              for maps in (1, 2, 4) for perspective in (False, True) for soft in (False, True)]
    assert all(result["max_visibility_error"] < 1e-5 for result in volume), volume
    if args.volume_baseline:
        baseline = args.volume_baseline.read_text(encoding="utf-8")
        baseline_volume = shadow_volume_error(ctx, baseline, 4, True, False)
        assert baseline_volume["max_visibility_error"] > .5, "Baseline no longer exercises invalid cascades"
        results["baseline_volume"] = baseline_volume
    if args.baseline:
        results["baseline_moving_map"] = [moving_map_flicker(ctx, args.baseline.read_text(encoding="utf-8"), radius)
                                          for radius in (1.0, 1.5)]
        assert all(r["max_change_per_eighth_texel"] > MAX_FLICKER[r["radius"]] for r in results["baseline_moving_map"]), \
            "Baseline no longer exercises the flicker"
        fbo.use()
    report = {"result": "passed", "renderer": ctx.info["GL_RENDERER"], "filters": results, "shader_variants_compiled": 4,
              "shadow_volume_variants_rendered": len(volume), "shadow_volume": volume}
    rendered = json.dumps(report, indent=2) + "\n"
    print(rendered, end="")
    if args.report:
        args.report.write_text(rendered, encoding="utf-8")
    fbo.release()
    ctx.release()


if __name__ == "__main__":
    main()