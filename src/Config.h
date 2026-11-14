#ifndef CONFIG_H
#define CONFIG_H

#include <QMap>
#include <QString>
#include <QStringList>

class QSettings;

// How a system (= folder name below GameInfo) is launched via RetroArch
struct SSystemLaunch {
    QString sCore;          // File name of the core without extension, e.g. "mame_libretro"
    QString sRomSubdir;     // Subfolder in the ROM directory
};

// Paths and external programs. Corresponds to the global variables from GetUserVariables()/GetLaunchData()
// of the AHK script; all values are stored in the INI file and can be adjusted there.
class CConfig
{
public:
    // Reads all values; missing entries are filled with Linux defaults and written back,
    // so they are visible and editable in the INI file.
    void Load(QSettings& settings);

    // No valid GameInfo folder configured?
    bool HasValidGameInfoDir() const;
    void SetGameInfoDir(QSettings& settings, const QString& sDir);

    QString FindCore(const QString& sCore) const;   // full path or empty

    QString                     sGameInfoDir;       // <UserD>/GameBrowser/GameInfo
    QString                     sThumbsCacheDir;    // base folder; contains "ThumbsCache<pixel height>"
    QString                     sRetroArch;         // program (PATH or absolute)
    QString                     sRetroArchConfig;
    QStringList                 aCoreDirs;          // searched in order
    QString                     sRomsDir;           // <UserD>/retroarch-roms
    QString                     sPcGamesDir;        // base for relative paths in Launch.conf of PC games
    QMap<QString, SSystemLaunch> mapSystems;        // system folder -> core/ROM subfolder
};

#endif // CONFIG_H
