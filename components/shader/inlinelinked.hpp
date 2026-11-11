#ifndef OPENMW_COMPONENTS_SHADER_INLINELINKED_H
#define OPENMW_COMPONENTS_SHADER_INLINELINKED_H

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// OpenGL ES 2 accepts exactly one shader object per stage, so the helper
// modules that desktop builds attach as separately compiled objects have to
// become part of the shader that uses them. Appending their source to it puts
// declarations shared by both into one translation unit, where GLSL ES
// rejects a second declaration of the same global.

namespace Shader
{
    namespace InlineLinkedDetail
    {
        inline std::string_view trim(std::string_view value)
        {
            constexpr std::string_view blank = " \t\r";
            const std::size_t first = value.find_first_not_of(blank);
            if (first == std::string_view::npos)
                return {};
            return value.substr(first, value.find_last_not_of(blank) - first + 1);
        }

        // Collapses whitespace and removes precision qualifiers.
        inline std::string normalize(std::string_view line)
        {
            std::string result;
            std::size_t position = 0;
            while (position < line.size())
            {
                const std::size_t start = line.find_first_not_of(" \t\r", position);
                if (start == std::string_view::npos)
                    break;
                std::size_t end = line.find_first_of(" \t\r", start);
                if (end == std::string_view::npos)
                    end = line.size();
                const std::string_view token = line.substr(start, end - start);
                position = end;
                if (token == "highp" || token == "mediump" || token == "lowp")
                    continue;
                if (!result.empty())
                    result += ' ';
                result += token;
            }
            return result;
        }

        inline bool startsWith(std::string_view value, std::string_view prefix)
        {
            return value.substr(0, prefix.size()) == prefix;
        }

        inline bool isGlobalDeclaration(std::string_view normalized)
        {
            if (normalized.empty() || normalized.back() != ';')
                return false;
            if (normalized.find('(') != std::string_view::npos || normalized.find('=') != std::string_view::npos)
                return false;
            return startsWith(normalized, "uniform ") || startsWith(normalized, "varying ")
                || startsWith(normalized, "centroid varying ") || startsWith(normalized, "attribute ");
        }

        // Returns the directive name of a preprocessor line and leaves its arguments in rest.
        inline std::string_view directive(std::string_view trimmed, std::string_view& rest)
        {
            if (trimmed.empty() || trimmed.front() != '#')
                return {};
            trimmed = trim(trimmed.substr(1));
            const std::size_t end = trimmed.find_first_of(" \t(");
            rest = end == std::string_view::npos ? std::string_view() : trim(trimmed.substr(end));
            return trimmed.substr(0, end);
        }

        inline std::vector<std::string_view> splitLines(std::string_view source)
        {
            std::vector<std::string_view> lines;
            std::size_t position = 0;
            while (position <= source.size())
            {
                std::size_t end = source.find('\n', position);
                if (end == std::string_view::npos)
                    end = source.size();
                lines.push_back(source.substr(position, end - position));
                position = end + 1;
            }
            return lines;
        }
    }

    class GlobalDeclarationFilter
    {
    public:
        // Records the global declarations of source. With linked set, also
        // blanks its #version line and every declaration already recorded
        // under the same preprocessor conditions.
        std::string process(std::string_view source, bool linked)
        {
            using namespace InlineLinkedDetail;
            const std::vector<std::string_view> lines = splitLines(source);
            std::vector<std::string> conditions;
            std::string result;
            result.reserve(source.size());
            for (std::size_t index = 0; index < lines.size(); ++index)
            {
                const std::string_view trimmed = trim(lines[index]);
                bool keep = true;
                std::string_view rest;
                const std::string_view name = directive(trimmed, rest);
                if (name == "if" || name == "ifdef")
                    conditions.push_back(normalize(trimmed));
                else if (name == "ifndef")
                {
                    // An include guard is always entered the first time and
                    // skipped by the preprocessor afterwards, so it does not
                    // distinguish two declarations.
                    std::string_view defined;
                    const bool guard = index + 1 < lines.size()
                        && directive(trim(lines[index + 1]), defined) == "define" && defined == rest;
                    conditions.push_back(guard ? std::string() : normalize(trimmed));
                }
                else if (name == "else" || name == "elif")
                {
                    if (!conditions.empty())
                        conditions.back() += " / " + normalize(trimmed);
                }
                else if (name == "endif")
                {
                    if (!conditions.empty())
                        conditions.pop_back();
                }
                else if (name == "version")
                    keep = !linked;
                else if (name.empty())
                {
                    const std::string normalized = normalize(trimmed);
                    if (isGlobalDeclaration(normalized))
                    {
                        std::string key;
                        for (const std::string& condition : conditions)
                            if (!condition.empty())
                                key += condition + '\n';
                        key += normalized;
                        const bool added = mSeen.insert(std::move(key)).second;
                        keep = added || !linked;
                    }
                }
                // Keep the line count so compiler messages still point at the right line.
                if (keep)
                    result += lines[index];
                if (index + 1 < lines.size())
                    result += '\n';
            }
            return result;
        }

    private:
        std::set<std::string> mSeen;
    };

    inline std::string inlineLinkedShaders(std::string_view source, const std::vector<std::string>& linkedSources)
    {
        GlobalDeclarationFilter filter;
        std::string result = filter.process(source, false);
        for (const std::string& linked : linkedSources)
        {
            if (result.empty() || result.back() != '\n')
                result += '\n';
            result += filter.process(linked, true);
        }
        return result;
    }
}

#endif
