#ifndef HELPERS_H
#define HELPERS_H

#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

// Taken from mkTools/shared/src/helpers.h (only the parts for launching programs/.desktop files)

struct DesktopEntry {
    QString id;
    QString name;
    QString icon;
    QString exec;
    QString path;
    QString workDir;
    QString type; // "Application", "Link", etc.
    QString url;  // for Type=Link
    bool isValid = false;
    bool useTerminal = false;
};

namespace Helpers {

    DesktopEntry getDesktopEntryById(const QString &id);
    DesktopEntry getDesktopEntry(const QFileInfo &fileInfo);
    void openFileListWithHandler(const QString &handler, const QStringList &fileList);
    void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList = {});
    QList<QStringList> expandDesktopExec(const DesktopEntry &entry, const QStringList &files);
    QString expandPath(QString path);

} // namespace Helpers

#endif // HELPERS_H
