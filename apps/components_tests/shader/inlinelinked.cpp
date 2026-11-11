#include <components/shader/inlinelinked.hpp>

#include <gtest/gtest.h>

namespace
{
    using Shader::inlineLinkedShaders;

    TEST(ShaderInlineLinkedTest, source_without_linked_modules_is_unchanged)
    {
        const std::string source = "#version 120\nuniform vec2 screenRes;\nvoid main() {}\n";
        EXPECT_EQ(inlineLinkedShaders(source, {}), source);
    }

    TEST(ShaderInlineLinkedTest, linked_module_loses_its_version_line_and_keeps_its_line_count)
    {
        const std::string source = "#version 120\nvec4 helper();\nvoid main() { helper(); }\n";
        const std::string linked = "#version 120\n\nvec4 helper() { return vec4(0.0); }\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }),
            "#version 120\nvec4 helper();\nvoid main() { helper(); }\n\n\nvec4 helper() { return vec4(0.0); }\n");
    }

    TEST(ShaderInlineLinkedTest, global_already_declared_by_the_shader_is_not_declared_again)
    {
        const std::string source = "uniform vec2 screenRes;\nvoid main() {}\n";
        const std::string linked = "uniform  vec2 screenRes;\nuniform float near;\nvoid f() {}\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }),
            "uniform vec2 screenRes;\nvoid main() {}\n\nuniform float near;\nvoid f() {}\n");
    }

    TEST(ShaderInlineLinkedTest, precision_qualifier_does_not_hide_a_repeated_declaration)
    {
        const std::string source = "uniform highp sampler2D opaqueDepthTex;\n";
        const std::string linked = "uniform sampler2D opaqueDepthTex;\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), "uniform highp sampler2D opaqueDepthTex;\n\n");
    }

    TEST(ShaderInlineLinkedTest, two_linked_modules_do_not_repeat_each_others_globals)
    {
        const std::string first = "uniform vec2 screenRes;\n";
        const std::string second = "uniform vec2 screenRes;\nuniform vec3 gridSize;\n";
        EXPECT_EQ(inlineLinkedShaders("void main() {}\n", { first, second }),
            "void main() {}\nuniform vec2 screenRes;\n\nuniform vec3 gridSize;\n");
    }

    TEST(ShaderInlineLinkedTest, declaration_inside_an_include_guard_matches_one_outside)
    {
        const std::string source
            = "#ifndef FRAGMENT_H\n#define FRAGMENT_H\n#if 1\nuniform sampler2D sky;\n#endif\n#endif\n";
        const std::string linked = "#if 1\nuniform sampler2D sky;\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + "#if 1\n\n#endif\n");
    }

    // The sun uniform of the phone crash: declared once by the shader and again
    // in the legacy branch of the linked lighting module.
    TEST(ShaderInlineLinkedTest, declaration_in_the_live_branch_of_a_constant_condition_is_a_duplicate)
    {
        const std::string source = "uniform DirectionalLight sun;\n";
        const std::string linked
            = "#if 0\nuniform DirectionalLight sun;\n#else\nuniform DirectionalLight sun;\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + "#if 0\n\n#else\n\n#endif\n");
    }

    TEST(ShaderInlineLinkedTest, dead_branches_are_blanked_and_live_ones_kept)
    {
        const std::string source = "#if 0\nuniform sampler2D sky;\n#endif\n";
        const std::string linked = "#if 1\nuniform sampler2D sky;\n#else\nuniform sampler2D sky;\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }),
            "#if 0\n\n#endif\n#if 1\nuniform sampler2D sky;\n#else\n\n#endif\n");
    }

    TEST(ShaderInlineLinkedTest, elif_chains_and_constant_expressions_are_resolved)
    {
        const std::string source = "#if !1 || 0\nA\n#elif (2 > 1) && 3 == 3\nB\n#else\nC\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { "" }), "#if !1 || 0\n\n#elif (2 > 1) && 3 == 3\nB\n#else\n\n#endif\n");
    }

    TEST(ShaderInlineLinkedTest, linked_include_guard_already_defined_by_the_shader_is_skipped)
    {
        const std::string source = "#ifndef G\n#define G\nstruct S { float x; };\n#endif\n";
        const std::string linked = "#ifndef G\n#define G\nstruct S { float x; };\n#endif\nvoid f() {}\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + "#ifndef G\n\n\n#endif\nvoid f() {}\n");
    }

    // The sky shader of the phone crash: its own copy of the material header
    // sits in a disabled block, so the linked copy must stay.
    TEST(ShaderInlineLinkedTest, include_guard_defined_only_in_dead_code_does_not_hide_the_linked_copy)
    {
        const std::string source = "#if 0\n#ifndef G\n#define G\nstruct S { float x; };\n#endif\n#endif\n";
        const std::string linked = "#ifndef G\n#define G\nstruct S { float x; };\n#endif\nuniform S s;\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), "#if 0\n\n\n\n\n#endif\n" + linked);
    }

    TEST(ShaderInlineLinkedTest, conditions_on_macros_are_left_to_the_compiler)
    {
        const std::string source = "#if PER_PIXEL_LIGHTING\nuniform sampler2D normalMap;\n#endif\n";
        const std::string linked = "uniform sampler2D normalMap;\n#ifdef GL_ES\nuniform sampler2D normalMap;\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + linked);
    }

    TEST(ShaderInlineLinkedTest, defined_uses_macros_seen_in_live_code)
    {
        const std::string source = "#define FOO 1\n#if defined(FOO) && !defined BAR\nA\n#endif\n#undef FOO\n";
        const std::string linked = "#if defined(FOO)\nB\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + "#if defined(FOO)\n\n#endif\n");
    }

    TEST(ShaderInlineLinkedTest, functions_and_initialised_globals_are_never_removed)
    {
        const std::string source = "uniform bool flag = true;\nvec4 helper(vec2 uv);\n";
        const std::string linked = "uniform bool flag = true;\nvec4 helper(vec2 uv);\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + linked);
    }
}
