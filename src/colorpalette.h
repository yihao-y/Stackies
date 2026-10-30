#pragma once
#include <QColor>
#include <functional>
class QWidget;
namespace sticky {
void showColorPalette(QWidget *parent,bool textPalette,const QColor &current,
                      const std::function<void(const QColor&)> &apply,
                      const std::function<void()> &reset={});
}
