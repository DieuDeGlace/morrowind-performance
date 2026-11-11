#ifndef OPENMW_COMPONENTS_SHADER_INLINELINKED_H
#define OPENMW_COMPONENTS_SHADER_INLINELINKED_H

#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// OpenGL ES 2 accepts exactly one shader object per stage, so the helper
// modules that desktop builds attach as separately compiled objects have to
// become part of the shader that uses them. Appending their source to it puts
// declarations shared by both into one translation unit, where GLSL ES
// rejects a second declaration of the same global.
//
// By the time this runs every @define has been substituted, so most
// preprocessor conditions are plain constants such as "#if 0" or
// "#if !1 || 0". Those are evaluated here: lines in branches that can never
// be compiled are blanked, which also keeps GL4ES from seeing include guards
// defined in dead code, and a declaration in a live branch matches the same
// declaration anywhere else live. Conditions that depend on macros stay as
// they are and only match declarations under the same conditions.

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

        inline std::string_view firstWord(std::string_view text)
        {
            text = trim(text);
            std::size_t end = 0;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
                ++end;
            return text.substr(0, end);
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

        // Evaluates an integer preprocessor expression made only of numbers
        // and operators. Anything else, such as a macro name, gives nullopt.
        class ConstantExpression
        {
        public:
            static std::optional<long long> evaluate(std::string_view text)
            {
                ConstantExpression parser(text);
                const std::optional<long long> value = parser.logicalOr();
                parser.skipBlank();
                if (!value || parser.mPosition != parser.mText.size())
                    return std::nullopt;
                return value;
            }

        private:
            explicit ConstantExpression(std::string_view text)
                : mText(text)
            {
            }

            void skipBlank()
            {
                while (mPosition < mText.size() && (mText[mPosition] == ' ' || mText[mPosition] == '\t'))
                    ++mPosition;
            }

            bool accept(std::string_view token)
            {
                skipBlank();
                if (mText.substr(mPosition, token.size()) != token)
                    return false;
                mPosition += token.size();
                return true;
            }

            // Accepts a one-character operator unless it starts a longer one.
            bool acceptSingle(char token, std::string_view notFollowedBy)
            {
                skipBlank();
                if (mPosition >= mText.size() || mText[mPosition] != token)
                    return false;
                if (mPosition + 1 < mText.size() && notFollowedBy.find(mText[mPosition + 1]) != std::string_view::npos)
                    return false;
                ++mPosition;
                return true;
            }

            std::optional<long long> logicalOr()
            {
                std::optional<long long> left = logicalAnd();
                while (left && accept("||"))
                {
                    const std::optional<long long> right = logicalAnd();
                    if (!right)
                        return std::nullopt;
                    left = (*left || *right) ? 1 : 0;
                }
                return left;
            }

            std::optional<long long> logicalAnd()
            {
                std::optional<long long> left = equality();
                while (left && accept("&&"))
                {
                    const std::optional<long long> right = equality();
                    if (!right)
                        return std::nullopt;
                    left = (*left && *right) ? 1 : 0;
                }
                return left;
            }

            std::optional<long long> equality()
            {
                std::optional<long long> left = relational();
                while (left)
                {
                    const bool equal = accept("==");
                    if (!equal && !accept("!="))
                        break;
                    const std::optional<long long> right = relational();
                    if (!right)
                        return std::nullopt;
                    left = ((*left == *right) == equal) ? 1 : 0;
                }
                return left;
            }

            std::optional<long long> relational()
            {
                std::optional<long long> left = additive();
                while (left)
                {
                    std::optional<long long> right;
                    if (accept("<="))
                        right = additive(), left = right ? std::optional<long long>(*left <= *right) : std::nullopt;
                    else if (accept(">="))
                        right = additive(), left = right ? std::optional<long long>(*left >= *right) : std::nullopt;
                    else if (acceptSingle('<', "<="))
                        right = additive(), left = right ? std::optional<long long>(*left < *right) : std::nullopt;
                    else if (acceptSingle('>', ">="))
                        right = additive(), left = right ? std::optional<long long>(*left > *right) : std::nullopt;
                    else
                        break;
                }
                return left;
            }

            std::optional<long long> additive()
            {
                std::optional<long long> left = multiplicative();
                while (left)
                {
                    const bool plus = acceptSingle('+', "+");
                    if (!plus && !acceptSingle('-', "-"))
                        break;
                    const std::optional<long long> right = multiplicative();
                    if (!right)
                        return std::nullopt;
                    left = plus ? *left + *right : *left - *right;
                }
                return left;
            }

            std::optional<long long> multiplicative()
            {
                std::optional<long long> left = unary();
                while (left)
                {
                    char operation = 0;
                    if (acceptSingle('*', ""))
                        operation = '*';
                    else if (acceptSingle('/', ""))
                        operation = '/';
                    else if (acceptSingle('%', ""))
                        operation = '%';
                    else
                        break;
                    const std::optional<long long> right = unary();
                    if (!right || (operation != '*' && *right == 0))
                        return std::nullopt;
                    left = operation == '*' ? *left * *right : operation == '/' ? *left / *right : *left % *right;
                }
                return left;
            }

            std::optional<long long> unary()
            {
                if (acceptSingle('!', "="))
                {
                    const std::optional<long long> value = unary();
                    return value ? std::optional<long long>(!*value) : std::nullopt;
                }
                if (acceptSingle('-', "-"))
                {
                    const std::optional<long long> value = unary();
                    return value ? std::optional<long long>(-*value) : std::nullopt;
                }
                if (acceptSingle('+', "+"))
                    return unary();
                return primary();
            }

            std::optional<long long> primary()
            {
                if (accept("("))
                {
                    const std::optional<long long> value = logicalOr();
                    if (!value || !accept(")"))
                        return std::nullopt;
                    return value;
                }
                skipBlank();
                const std::size_t start = mPosition;
                long long value = 0;
                while (mPosition < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPosition])))
                    value = value * 10 + (mText[mPosition++] - '0');
                if (mPosition == start)
                    return std::nullopt;
                return value;
            }

            std::string_view mText;
            std::size_t mPosition = 0;
        };
    }

    class GlobalDeclarationFilter
    {
    public:
        // Blanks lines in branches that can never compile and records the
        // global declarations of source. With linked set, also blanks its
        // #version line and every declaration already recorded under the
        // same unresolved conditions. Line counts never change, so compiler
        // messages still point at the right line.
        std::string process(std::string_view source, bool linked)
        {
            using namespace InlineLinkedDetail;
            const std::vector<std::string_view> lines = splitLines(source);
            std::vector<Branch> branches;
            std::string result;
            result.reserve(source.size());
            for (std::size_t index = 0; index < lines.size(); ++index)
            {
                const std::string_view trimmed = trim(lines[index]);
                std::string_view rest;
                const std::string_view name = directive(trimmed, rest);
                const State enclosing = combined(branches);
                bool keep = enclosing != State::Dead;

                if (name == "if" || name == "ifdef" || name == "ifndef")
                {
                    State state = State::Dead;
                    if (enclosing != State::Dead)
                    {
                        if (name == "if")
                            state = evaluate(rest);
                        else
                        {
                            const std::string macro(firstWord(rest));
                            std::string_view defined;
                            const bool guard = name == "ifndef" && index + 1 < lines.size()
                                && directive(trim(lines[index + 1]), defined) == "define"
                                && firstWord(defined) == macro;
                            state = macroState(macro, name == "ifndef", guard);
                        }
                    }
                    branches.push_back({ state, state == State::Live, state == State::Unknown,
                        state == State::Unknown ? normalize(trimmed) : std::string() });
                }
                else if (name == "elif" || name == "else")
                {
                    keep = combined(branches, 1) != State::Dead;
                    if (!branches.empty())
                    {
                        Branch& branch = branches.back();
                        State state;
                        if (combined(branches, 1) == State::Dead || branch.mTaken)
                            state = State::Dead;
                        else if (branch.mUncertain)
                            state = State::Unknown;
                        else
                            state = name == "else" ? State::Live : evaluate(rest);
                        branch.mState = state;
                        branch.mTaken = branch.mTaken || state == State::Live;
                        branch.mUncertain = branch.mUncertain || state == State::Unknown;
                        branch.mText += " / " + normalize(trimmed);
                    }
                }
                else if (name == "endif")
                {
                    keep = combined(branches, 1) != State::Dead;
                    if (!branches.empty())
                        branches.pop_back();
                }
                else if (keep && (name == "define" || name == "undef"))
                {
                    const std::string macro(firstWord(rest));
                    if (enclosing == State::Live)
                        mDefines[macro] = name == "define";
                    else
                        mDefines[macro] = std::nullopt;
                }
                else if (keep && name == "version")
                    keep = !linked;
                else if (keep && name.empty())
                {
                    const std::string normalized = normalize(trimmed);
                    if (isGlobalDeclaration(normalized))
                    {
                        std::string key;
                        for (const Branch& branch : branches)
                            if (branch.mState == State::Unknown)
                                key += branch.mText + '\n';
                        key += normalized;
                        const bool added = mSeen.insert(std::move(key)).second;
                        keep = added || !linked;
                    }
                }

                if (keep)
                    result += lines[index];
                if (index + 1 < lines.size())
                    result += '\n';
            }
            return result;
        }

    private:
        enum class State
        {
            Live,
            Dead,
            Unknown
        };

        struct Branch
        {
            State mState;
            bool mTaken;
            bool mUncertain;
            std::string mText;
        };

        // The state of code inside all branches but the innermost skip ones.
        static State combined(const std::vector<Branch>& branches, std::size_t skip = 0)
        {
            State state = State::Live;
            for (std::size_t index = 0; index + skip < branches.size(); ++index)
            {
                if (branches[index].mState == State::Dead)
                    return State::Dead;
                if (branches[index].mState == State::Unknown)
                    state = State::Unknown;
            }
            return state;
        }

        static State fromBool(bool value) { return value ? State::Live : State::Dead; }

        State macroState(const std::string& macro, bool negate, bool guard) const
        {
            const auto found = mDefines.find(macro);
            if (found == mDefines.end())
                // A macro never seen is only known to be undefined for an
                // include guard; others may come from the driver or GL4ES.
                return guard ? State::Live : State::Unknown;
            if (!found->second)
                return State::Unknown;
            return fromBool(*found->second != negate);
        }

        State evaluate(std::string_view expression) const
        {
            using namespace InlineLinkedDetail;
            std::string text;
            std::size_t position = 0;
            while (position < expression.size())
            {
                const std::size_t start = expression.find("defined", position);
                if (start == std::string_view::npos)
                {
                    text += expression.substr(position);
                    break;
                }
                text += expression.substr(position, start - position);
                std::size_t cursor = start + 7;
                while (cursor < expression.size() && (expression[cursor] == ' ' || expression[cursor] == '('))
                    ++cursor;
                const std::string macro(firstWord(expression.substr(cursor)));
                cursor += macro.size();
                while (cursor < expression.size() && (expression[cursor] == ' ' || expression[cursor] == ')'))
                    ++cursor;
                const auto found = mDefines.find(macro);
                if (macro.empty() || found == mDefines.end() || !found->second)
                    return State::Unknown;
                text += *found->second ? "1" : "0";
                position = cursor;
            }
            const std::optional<long long> value = ConstantExpression::evaluate(text);
            return value ? fromBool(*value != 0) : State::Unknown;
        }

        std::set<std::string> mSeen;
        // Macros seen so far: defined, undefined, or nullopt when that
        // depends on a condition this filter cannot resolve.
        std::map<std::string, std::optional<bool>> mDefines;
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
