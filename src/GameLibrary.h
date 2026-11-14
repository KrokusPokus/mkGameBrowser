#ifndef GAMELIBRARY_H
#define GAMELIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>

class CConfig;

// Fields that can be used for grouping, sorting and labeling
// (keys as in the AHK script: "Auto_Series_STR", ...)
enum class EField {
    None,
    Series,
    Name,
    DevName,
    System,
    Genre,
    ReleaseDate,
    ReleaseDateYM       // grouping only (year/month)
};

QString FieldKey(EField eField);                        // "Auto_System_STR", or "|" for None
EField  FieldFromKey(const QString& sKey, EField eFallback);

// Contents of a GameInfo.txt
struct SGameInfo {
    QString sSeries;
    QString sName;              // serialized (\n, \t, \", \\)
    QString sDevName;
    QString sSystem;
    QString sGenre;             // multiple values separated by "|"
    QString sReleaseDate;       // "YYYY-MM-DD", "YYYY", "tba", ...
    QString sComments;          // Private_Comments_STR, serialized
};

// One game (= folder GameInfo/<System>/<Game>), corresponds to obj_Volume from GetGameObj()
struct SGame {
    SGameInfo info;
    QString   sGamePath;        // game folder
    QString   sCoverPath;       // full path to Cover.jpg/.png or empty
    QString   sThumbMarker;     // "<System>_<Game>" -> file name in the thumbnail cache
    bool      bHaveReviews = false;
    bool      bInstalled   = false;

    // set by the filter (ShowThisThumb / InactiveOverlay)
    bool      bVisible = true;
    bool      bShaded  = false;

    QString Field(EField eField) const;
};

// Result of GetLaunchData()
struct SLaunchData {
    bool        bAvailable = false;
    QString     sProgram;
    QStringList aArgs;
    QString     sWorkingDir;
    QString     sMissing;       // what is missing (for error messages)
};

namespace GameLibrary {

// Reads all games from <GameInfo>/<System>/<Game> (folders starting with "_" are ignored).
// As in the original, a missing GameInfo.txt is created, an existing one is normalized, and a
// missing Launch*.conf is created with a placeholder.
QList<SGame> Load(const CConfig& config, QStringList* pErrors);

// Read only, write nothing (for the info popup)
SGameInfo ReadGameInfo(const QString& sGamePath);

SLaunchData GetLaunchData(const QString& sConfPath, const CConfig& config);

QStringList ConfFiles(const QString& sGamePath);        // *.conf, naturally sorted
QStringList FilesIn(const QString& sDir);               // non-hidden files, naturally sorted
QString ChildCaseInsensitive(const QString& sDir, const QString& sName);

QString Unserialize(QString s);                         // UnserializeString()
int CompareLogical(const QString& s1, const QString& s2); // StrCmpLogicalW

} // namespace GameLibrary

#endif // GAMELIBRARY_H
