#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QString>
#include <QtGlobal>

namespace sticky {

// Keep the same OS-specific base directory (including Roaming on Windows).
// Some Qt versions/platforms append organization and application names,
// producing .../Stackies/Stackies when both names are "Stackies".
// Remove only that exact duplicated trailing component.
inline QString collapseDuplicateStackiesDirectory(const QString &path){
    if(path.isEmpty())return path;
    const QFileInfo application(QDir::cleanPath(path));
    const QFileInfo organization(application.path());
    if(application.fileName()==QStringLiteral("Stackies") &&
       organization.fileName()==QStringLiteral("Stackies"))
        return organization.filePath();
    return QDir::cleanPath(path);
}

inline QString stackiesDefaultDataDirectory(){
    return collapseDuplicateStackiesDirectory(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

inline QString stackiesDataDirectory(){
    const QSettings settings;
    // The GUI selection always wins, including "Restore Default" (empty string).
    // The environment only supplies a fallback for fresh setups and tests.
    if(settings.contains("storage/dataDirectory")){
        const QString custom=settings.value("storage/dataDirectory").toString();
        return custom.isEmpty()?stackiesDefaultDataDirectory():QDir::cleanPath(custom);
    }
    const QString override=QString::fromLocal8Bit(qgetenv("STACKIES_DATA_DIR"));
    if(!override.isEmpty())return QDir::cleanPath(override);
    return stackiesDefaultDataDirectory();
}

} // namespace sticky
