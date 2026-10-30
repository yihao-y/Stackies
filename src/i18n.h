#pragma once
#include <QCoreApplication>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QFont>
class QApplication;
class QTranslator;

// User-facing Qt strings share a stable context, independently of the OS.
// A manually chosen UI language overrides the operating-system locale.
namespace sticky {
inline QString trUi(const char *source){
    return QCoreApplication::translate("Stackies",source);
}
QString systemUiLocale(const QLocale &locale);
QString effectiveUiLocale();
QStringList supportedUiLocales();
bool loadUiTranslator(QTranslator &translator,const QString &language);
// Test the actual Qt font fallback before showing non-Latin menu labels.
bool canDisplayUiText(const QFont &font,const QString &text);
QString readableLanguageLabel(const QString &nativeName,const QString &englishName,
                              const QFont &menuFont);
// Representative UI glyphs for the requested locale; does not guarantee all text.
bool hasUiLanguageFont(const QString &language);
void applyUiFont(QApplication &app,const QString &language);
// One font choice for note text in editor, reader, drag copy and Overview.
QFont noteContentFont();
QString initialWelcomeTitle();
QString initialWelcomeHtml();
}
