#pragma once
#include <QPoint>
#include <QWidget>
#include <vector>

// Native desktop policies live here; presentation and Workspace logic should
// not need Windows headers or platform-specific window identifiers.
namespace sticky::platform {
void raiseStack(QWidget *window, bool permanentlyTopmost);
// Toggle in place on Windows and Linux/X11; other backends use Qt fallback.
void setStackTopmost(QWidget *window, bool topmost);
bool raiseForDrag(QWidget *window, bool permanentlyTopmost);
void restoreAfterDrag(QWidget *window, bool wasTemporarilyTopmost);
QWidget *stackAt(const QPoint &global, const std::vector<QWidget*> &candidates,
                 QWidget *dragged, QWidget *mergePreview, QWidget *tabPreview);
// Native Z-order hit-testing currently exists on Windows only. Other
// platforms use the legacy geometry fallback pending native verification.
bool hasReliableForeignWindowHitTest();
}
