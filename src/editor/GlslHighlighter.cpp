#include "editor/GlslHighlighter.h"

namespace pf {

namespace {

QTextCharFormat makeFormat(const QColor& color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold)
        f.setFontWeight(QFont::DemiBold);
    f.setFontItalic(italic);
    return f;
}

QRegularExpression wordList(const QStringList& words)
{
    return QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(words.join(u'|')));
}

} // namespace

GlslHighlighter::GlslHighlighter(QTextDocument* document) : QSyntaxHighlighter(document)
{
    const QStringList keywords = {
        "attribute", "const", "uniform", "varying", "buffer", "shared", "coherent", "volatile",
        "restrict", "readonly", "writeonly", "layout", "centroid", "flat", "smooth",
        "noperspective", "patch", "sample", "break", "continue", "do", "for", "while", "switch",
        "case", "default", "if", "else", "subroutine", "in", "out", "inout", "true", "false",
        "invariant", "precise", "discard", "return", "struct", "precision", "highp", "mediump",
        "lowp"};
    const QStringList types = {
        "void", "bool", "int", "uint", "float", "double", "vec2", "vec3", "vec4", "dvec2",
        "dvec3", "dvec4", "bvec2", "bvec3", "bvec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3",
        "uvec4", "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3",
        "mat3x4", "mat4x2", "mat4x3", "mat4x4", "sampler1D", "sampler2D", "sampler3D",
        "samplerCube", "sampler2DShadow", "sampler2DArray", "isampler2D", "usampler2D",
        "image2D", "uimage2D", "iimage2D"};
    const QStringList builtins = {
        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh",
        "tanh", "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign",
        "floor", "trunc", "round", "ceil", "fract", "mod", "modf", "min", "max", "clamp", "mix",
        "step", "smoothstep", "isnan", "isinf", "fma", "length", "distance", "dot", "cross",
        "normalize", "faceforward", "reflect", "refract", "matrixCompMult", "outerProduct",
        "transpose", "determinant", "inverse", "lessThan", "greaterThan", "equal", "notEqual",
        "any", "all", "not", "texture", "textureLod", "texelFetch", "textureSize", "dFdx",
        "dFdy", "fwidth", "gl_Position", "gl_FragCoord", "gl_FrontFacing", "gl_VertexID",
        "gl_InstanceID", "gl_FragDepth", "gl_PointCoord"};

    m_rules.push_back({wordList(keywords), makeFormat(QColor(0xc6, 0x95, 0xc6), true)});
    m_rules.push_back({wordList(types), makeFormat(QColor(0x6c, 0xb6, 0xd9))});
    m_rules.push_back({wordList(builtins), makeFormat(QColor(0xdc, 0xc6, 0x8a))});
    m_rules.push_back({QRegularExpression(QStringLiteral(
                           R"(\b(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFuU]?(?:lf|LF)?\b)")),
                       makeFormat(QColor(0xb5, 0xce, 0xa8))});
    m_rules.push_back({QRegularExpression(QStringLiteral(R"(^\s*#\s*\w+.*$)")),
                       makeFormat(QColor(0x9b, 0x9b, 0x9b), false, true)});

    m_commentFormat = makeFormat(QColor(0x6a, 0x74, 0x7d), false, true);
    m_rules.push_back({QRegularExpression(QStringLiteral("//[^\n]*")), m_commentFormat});
}

void GlslHighlighter::highlightBlock(const QString& text)
{
    for (const Rule& rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()), rule.format);
        }
    }

    // Multi-line /* */ comments; block state 1 = inside a comment.
    setCurrentBlockState(0);
    qsizetype start = 0;
    if (previousBlockState() != 1)
        start = text.indexOf(m_blockCommentStart);
    while (start >= 0) {
        const auto end = m_blockCommentEnd.match(text, start);
        qsizetype length;
        if (!end.hasMatch()) {
            setCurrentBlockState(1);
            length = text.length() - start;
        } else {
            length = end.capturedEnd() - start;
        }
        setFormat(int(start), int(length), m_commentFormat);
        start = text.indexOf(m_blockCommentStart, start + length);
    }
}

} // namespace pf
