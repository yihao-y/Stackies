#pragma once
#include <QString>

// Consistent vertical scrollbar for note reading, editing and Overview.
// Qt retains native scrolling/input behavior; only the visual skin changes.
namespace sticky {
inline QString verticalScrollBarStyle(){
    return QStringLiteral(R"QSS(
        QScrollBar:vertical {
            width: 8px;
            margin: 0px;
            border: none;
            background: transparent;
        }
        QScrollBar::handle:vertical {
            min-height: 28px;
            margin: 1px;
            border: none;
            border-radius: 3px;
            background: rgba(82, 98, 112, 112);
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(73, 91, 108, 175);
        }
        QScrollBar::handle:vertical:pressed {
            background: rgba(61, 80, 99, 205);
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0px;
            border: none;
            background: transparent;
        }
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar::up-arrow:vertical,
        QScrollBar::down-arrow:vertical {
            width: 0px;
            height: 0px;
            background: transparent;
        }
    )QSS");
}
} // namespace sticky
