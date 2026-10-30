#pragma once
#include <QString>
namespace sticky {
// Local diagnostics never intentionally log note bodies or titles.
void installDiagnostics();
QString diagnosticsDirectory();
}
