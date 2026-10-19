#include "app/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace pf {

void applyTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(0x2b, 0x2d, 0x31);
    const QColor base(0x1e, 0x1f, 0x22);
    const QColor alternate(0x24, 0x25, 0x29);
    const QColor text(0xd4, 0xd6, 0xda);
    const QColor dimText(0x80, 0x84, 0x8b);
    const QColor button(0x33, 0x35, 0x3a);
    const QColor highlight(0x3d, 0x6f, 0xa8);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dimText);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6c, 0xa6, 0xe0));
    p.setColor(QPalette::Mid, QColor(0x3a, 0x3c, 0x41));
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1b, 0x1e));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dimText);
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow::separator { background: #1a1b1e; width: 3px; height: 3px; }
        QDockWidget { titlebar-close-icon: none; }
        QDockWidget::title {
            background: #26282b; padding: 4px 8px; border-bottom: 1px solid #1a1b1e;
        }
        QToolBar { background: #26282b; border: none; border-bottom: 1px solid #1a1b1e;
                   spacing: 2px; padding: 2px 4px; }
        QToolButton { padding: 3px 8px; border: 1px solid transparent; border-radius: 2px; }
        QToolButton:hover { background: #34363b; border-color: #3e4046; }
        QToolButton:checked { background: #2f3f52; border-color: #3d6fa8; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { background: #26282b; color: #9a9da3; padding: 4px 12px;
                       border: 1px solid #1a1b1e; border-bottom: none; }
        QTabBar::tab:selected { background: #1e1f22; color: #d4d6da; }
        QStatusBar { background: #26282b; border-top: 1px solid #1a1b1e; }
        QStatusBar QLabel { color: #9a9da3; padding: 0 6px; }
        QHeaderView::section { background: #26282b; color: #9a9da3; padding: 3px 6px;
                               border: none; border-right: 1px solid #1a1b1e;
                               border-bottom: 1px solid #1a1b1e; }
        QLabel#diagnosticsSummary { background: #24262a; border-bottom: 1px solid #1a1b1e; }
        QLabel#diagnosticsSummary[state="ok"] { color: #8fbf7f; }
        QLabel#diagnosticsSummary[state="error"] { color: #e06c6c; }
    )"));
}

} // namespace pf
