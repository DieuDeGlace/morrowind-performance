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

    TEST(ShaderInlineLinkedTest, declaration_under_a_different_condition_is_kept)
    {
        const std::string source = "#if 0\nuniform sampler2D sky;\n#endif\n";
        const std::string linked = "#if 1\nuniform sampler2D sky;\n#else\nuniform sampler2D sky;\n#endif\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + linked);
    }

    TEST(ShaderInlineLinkedTest, functions_and_initialised_globals_are_never_removed)
    {
        const std::string source = "uniform bool flag = true;\nvec4 helper(vec2 uv);\n";
        const std::string linked = "uniform bool flag = true;\nvec4 helper(vec2 uv);\n";
        EXPECT_EQ(inlineLinkedShaders(source, { linked }), source + linked);
    }
}
