#include "shaders/syntax.h"

#include <string_view>

namespace gfx {
namespace {
using Color = TextEditor::PaletteIndex;
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           static_cast<unsigned char>(c) >= 128;
}
bool Id(char c) { return Alpha(c) || Digit(c); }
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool Starts(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}
template<size_t N>
bool In(std::string_view text, const std::string_view (&words)[N]) {
    for (const auto word : words)
        if (word == text) return true;
    return false;
}

bool Tokenize(const char* begin, const char* end, const char*& first, const char*& last,
              Color& color, bool spirv) {
    while (begin != end && Space(*begin)) ++begin;
    first = begin;
    last = begin;
    color = Color::Default;
    if (begin == end) return false;
    const char c = *begin;
    const char* p = begin + 1;
    if ((spirv && c == ';') || (!spirv && c == '/' && p != end && *p == '/')) {
        last = end;
        color = Color::Comment;
    } else if (!spirv && c == '/' && p != end && *p == '*') {
        ++p;
        while (p != end && !(p[-1] == '*' && *p == '/')) ++p;
        last = p == end ? p : p + 1;
        color = Color::MultiLineComment;
    } else if (c == '"' || (!spirv && c == '\'')) {
        while (p != end) {
            const char next = *p++;
            if (next == '\\' && p != end) ++p;
            else if (next == c) break;
        }
        last = p;
        color = c == '"' ? Color::String : Color::CharLiteral;
    } else if (!spirv && c == '#') {
        while (p != end && (*p == ' ' || *p == '\t')) ++p;
        while (p != end && Id(*p)) ++p;
        last = p;
        color = Color::Preprocessor;
    } else if (spirv && c == '%') {
        while (p != end && Id(*p)) ++p;
        last = p;
        color = Color::Identifier;
    } else if (Digit(c) || (c == '.' && p != end && Digit(*p)) ||
               (spirv && (c == '-' || c == '+') && p != end &&
                (Digit(*p) || (*p == '.' && p + 1 != end && Digit(p[1]))))) {
        const char* digits = (c == '-' || c == '+') ? p : begin;
        const bool hex = end - digits >= 2 && digits[0] == '0' &&
                         (digits[1] == 'x' || digits[1] == 'X');
        p = hex ? digits + 2 : digits;
        while (p != end && (Digit(*p) || *p == '.' ||
               (hex && ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))))) ++p;
        if (p != end && (hex ? (*p == 'p' || *p == 'P') : (*p == 'e' || *p == 'E'))) {
            ++p;
            if (p != end && (*p == '+' || *p == '-')) ++p;
            while (p != end && Digit(*p)) ++p;
        }
        while (p != end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L' || *p == 'f' || *p == 'F')) ++p;
        last = p;
        color = Color::Number;
    } else if (Alpha(c)) {
        while (p != end && Id(*p)) ++p;
        last = p;
        const std::string_view word(begin, static_cast<size_t>(p - begin));
        if (spirv) {
            color = Starts(word, "Op") ? Color::Keyword : Color::KnownIdentifier;
        } else {
            static constexpr std::string_view keywords[] = {
                "attribute", "const", "uniform", "varying", "buffer", "shared", "coherent",
                "volatile", "restrict", "readonly", "writeonly", "atomic_uint", "layout",
                "centroid", "flat", "smooth", "noperspective", "patch", "sample", "invariant",
                "precise", "break", "continue", "do", "for", "while", "switch", "case",
                "default", "if", "else", "subroutine", "in", "out", "inout", "float", "double",
                "int", "void", "bool", "true", "false", "uint", "lowp", "mediump", "highp",
                "precision", "discard", "return", "struct", "demote", "terminateInvocation"};
            static constexpr std::string_view builtins[] = {
                "abs", "acos", "acosh", "all", "any", "asin", "asinh", "atan", "atanh",
                "atomicAdd", "atomicAnd", "atomicCompSwap", "atomicExchange", "atomicMax",
                "atomicMin", "atomicOr", "atomicXor", "barrier", "bitCount", "bitfieldExtract",
                "ceil", "clamp", "cos", "cross", "dFdx", "dFdy", "degrees", "distance", "dot",
                "exp", "exp2", "floor", "fma", "fract", "fwidth", "imageLoad", "imageStore",
                "inversesqrt", "length", "log", "log2", "max", "memoryBarrier", "min", "mix",
                "mod", "normalize", "pow", "radians", "reflect", "refract", "round", "sign",
                "sin", "smoothstep", "sqrt", "step", "tan", "texelFetch", "texture", "textureLod",
                "textureSize", "transpose", "trunc"};
            const bool vector = Starts(word, "vec") || Starts(word, "ivec") || Starts(word, "uvec") ||
                Starts(word, "bvec") || Starts(word, "dvec") || Starts(word, "mat") || Starts(word, "dmat");
            // Validate vector/matrix dimensions; user identifiers such as "material" are not types.
            const auto dim = word.find_first_of("234");
            bool dimensionType = vector && dim != std::string_view::npos;
            if (dimensionType) {
                const auto base = word.substr(0, dim);
                static constexpr std::string_view bases[] = {"vec", "ivec", "uvec", "bvec", "dvec", "mat", "dmat"};
                dimensionType = In(base, bases) && (word.size() == dim + 1 ||
                    ((base == "mat" || base == "dmat") && word.size() == dim + 3 && word[dim + 1] == 'x' &&
                     word.back() >= '2' && word.back() <= '4'));
            }
            static constexpr std::string_view opaque[] = {
                "sampler", "samplerShadow", "sampler1D", "sampler2D", "sampler3D", "samplerCube",
                "sampler2DArray", "samplerCubeArray", "sampler2DShadow", "sampler2DArrayShadow",
                "sampler2DMS", "sampler2DMSArray", "isampler2D", "usampler2D", "image1D", "image2D",
                "image3D", "imageCube", "image2DArray", "image2DMS", "iimage2D", "uimage2D",
                "subpassInput", "isubpassInput", "usubpassInput", "subpassInputMS", "accelerationStructureEXT"};
            if (In(word, keywords) || dimensionType || In(word, opaque)) color = Color::Keyword;
            else if (Starts(word, "gl_") || In(word, builtins)) color = Color::KnownIdentifier;
            else color = Color::Identifier;
        }
    } else {
        last = p;
        color = Color::Punctuation;
    }
    return true;
}
} // namespace

bool TokenizeGlsl(const char* b, const char* e, const char*& f, const char*& l, Color& c) {
    return Tokenize(b, e, f, l, c, false);
}
bool TokenizeSpirv(const char* b, const char* e, const char*& f, const char*& l, Color& c) {
    return Tokenize(b, e, f, l, c, true);
}
const TextEditor::LanguageDefinition& GlslLanguage() {
    static const auto language = [] {
        TextEditor::LanguageDefinition d;
        d.mName = "Vulkan GLSL";
        d.mTokenize = TokenizeGlsl;
        d.mCommentStart = "/*";
        d.mCommentEnd = "*/";
        d.mSingleLineComment = "//";
        return d;
    }();
    return language;
}
const TextEditor::LanguageDefinition& SpirvLanguage() {
    static const auto language = [] {
        TextEditor::LanguageDefinition d;
        d.mName = "SPIR-V assembly";
        d.mTokenize = TokenizeSpirv;
        d.mCommentStart = "\x01"; // non-empty impossible markers avoid upstream empty-delimiter matching
        d.mCommentEnd = "\x02";
        d.mSingleLineComment = ";";
        d.mPreprocChar = '\0';
        d.mAutoIndentation = false;
        return d;
    }();
    return language;
}
} // namespace gfx
