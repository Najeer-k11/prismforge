#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace pf {

class GlslHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit GlslHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    QRegularExpression m_blockCommentStart{QStringLiteral(R"(/\*)")};
    QRegularExpression m_blockCommentEnd{QStringLiteral(R"(\*/)")};
};

} // namespace pf
