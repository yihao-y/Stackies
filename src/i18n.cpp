#include "i18n.h"
#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QDebug>
#include <QToolTip>
#include <QSettings>
#include <QTranslator>
#include <QStringList>
#include <QFont>

namespace sticky {
QString systemUiLocale(const QLocale &locale){
    switch(locale.language()){
    case QLocale::Chinese:
        return (locale.script()==QLocale::TraditionalChineseScript ||
                locale.territory()==QLocale::Taiwan ||
                locale.territory()==QLocale::HongKong ||
                locale.territory()==QLocale::Macau) ? "zh_TW" : "zh_CN";
    case QLocale::Japanese:return "ja";
    case QLocale::Korean:return "ko";
    case QLocale::Spanish:return "es";
    default:return "en";
    }
}
QStringList supportedUiLocales(){
    return {"en","zh_CN","zh_TW","ja","ko","es"};
}
namespace {
QStringList preferredFamilies(const QString &language){
#if defined(Q_OS_WIN)
    if(language=="zh_CN")return {"Microsoft YaHei UI","Microsoft YaHei","Noto Sans CJK SC","PingFang SC"};
    if(language=="zh_TW")return {"Microsoft JhengHei UI","Noto Sans CJK TC","PingFang TC"};
    if(language=="ja")return {"Yu Gothic UI","Noto Sans CJK JP","Hiragino Sans"};
    if(language=="ko")return {"Malgun Gothic","Noto Sans CJK KR","Apple SD Gothic Neo"};
#elif defined(Q_OS_MACOS)
    if(language=="zh_CN")return {"PingFang SC","Noto Sans CJK SC","Microsoft YaHei UI"};
    if(language=="zh_TW")return {"PingFang TC","Noto Sans CJK TC","Microsoft JhengHei UI"};
    if(language=="ja")return {"Hiragino Sans","Noto Sans CJK JP","Yu Gothic UI"};
    if(language=="ko")return {"Apple SD Gothic Neo","Noto Sans CJK KR","Malgun Gothic"};
#else
    if(language=="zh_CN")return {"Noto Sans CJK SC","Source Han Sans SC","Microsoft YaHei UI"};
    if(language=="zh_TW")return {"Noto Sans CJK TC","Source Han Sans TC","Microsoft JhengHei UI"};
    if(language=="ja")return {"Noto Sans CJK JP","Hiragino Sans","Yu Gothic UI"};
    if(language=="ko")return {"Noto Sans CJK KR","Malgun Gothic","Apple SD Gothic Neo"};
#endif
    return {};
}
QStringList installedPreferredFamilies(const QString &language){
    QStringList installed;
    for(const QString &family:preferredFamilies(language))
        if(QFontDatabase::hasFamily(family))installed.append(family);
    return installed;
}
QFont candidateFont(const QString &language){
    QFont font=QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    const auto installed=installedPreferredFamilies(language);
    if(!installed.isEmpty())font.setFamily(installed.first());
    return font;
}
QString probeForLanguage(const QString &language){
    if(language=="zh_CN")return QStringLiteral("简体中文欢迎便签编辑设置保存删除");
    if(language=="zh_TW")return QStringLiteral("繁體中文歡迎便箋編輯設定儲存刪除");
    if(language=="ja")return QStringLiteral("日本語ようこそ付箋編集設定保存");
    if(language=="ko")return QStringLiteral("한국어환영합니다메모설정저장편집");
    if(language=="es")return QStringLiteral("Español¡Bienvenido!Configuración");
    return QStringLiteral("English");
}
}
bool canDisplayUiText(const QFont &font,const QString &text){
    // QFontMetrics queries Qt's font engine, including platform fallback.
    // A missing glyph becomes tofu; never display such a menu label.
    const QFontMetrics metrics(font);
    for(const QChar ch:text){
        if(ch.isSurrogate())return false; // UI labels currently use BMP text.
        if(!metrics.inFontUcs4(ch.unicode()))return false;
    }
    return true;
}
QString readableLanguageLabel(const QString &nativeName,const QString &englishName,
                              const QFont &menuFont){
    return canDisplayUiText(menuFont,nativeName)?nativeName:englishName;
}
bool hasUiLanguageFont(const QString &language){
    if(language=="en")return true;
    if(!supportedUiLocales().contains(language))return false;
    return canDisplayUiText(candidateFont(language),probeForLanguage(language));
}
QString effectiveUiLocale(){
    const QString requested=QSettings().value("ui/language","system").toString();
    const QString chosen=requested=="system"?systemUiLocale(QLocale::system()):
        (supportedUiLocales().contains(requested)?requested:QStringLiteral("en"));
    // If the user removed a font since last launch (or the system locale
    // refers to a missing script), prefer a readable English UI over tofu.
    if(!hasUiLanguageFont(chosen)){
        qWarning().noquote()<<"Unavailable UI script on startup:"<<chosen
                            <<"; using English for this session";
        return QStringLiteral("en");
    }
    return chosen;
}
bool loadUiTranslator(QTranslator &translator,const QString &language){
    if(language=="en")return true; // English is the source language.
    if(!supportedUiLocales().contains(language))return false;
    return translator.load(":/i18n/stackies_"+language+".qm");
}
void applyUiFont(QApplication &app,const QString &language){
    // Use the same candidate for both the pre-switch glyph test and the
    // actual font applied after restarting the application.
    const QStringList installed=installedPreferredFamilies(language);
    const QFont font=candidateFont(language);
#if QT_VERSION >= QT_VERSION_CHECK(6,8,0)
    if(language=="zh_CN" || language=="zh_TW")
        QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han,installed);
#endif
    app.setFont(font);
    const QFontInfo resolved(font);
    qInfo().noquote() << "UI font requested:" << font.family()
                      << "resolved:" << resolved.family()
                      << "locale:" << language
                      << "Han candidates:" << installed;
    // These two classes can consult a platform/style-specific font instead
    // of the QWidget application default. Keep both in the same UI family.
    QApplication::setFont(font,"QMenu");
    QToolTip::setFont(font);
}
QFont noteContentFont(){
    // Preserve the editor's historical 11pt size while sharing the selected
    // script-specific family and fallback policy across all render paths.
    QFont font=QApplication::font();
    font.setPointSize(11);
    return font;
}
QString initialWelcomeTitle(){return trUi("Welcome");}
QString initialWelcomeHtml(){
    return QStringLiteral("<h3>%1</h3><p>%2</p><p>%3</p>")
        .arg(trUi("Welcome to Stackies!").toHtmlEscaped(),
             trUi("Double-click to edit this note.").toHtmlEscaped(),
             trUi("Drag notes together to stack them.").toHtmlEscaped());
}
}
