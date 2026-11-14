#include "GameLibrary.h"

#include "Config.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QCollator& LogicalCollator()
{
    static const QCollator s_collator = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    return s_collator;
}

QString ReadText(const QString& sPath, bool* pOk = nullptr)
{
    QFile f(sPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (pOk) *pOk = false;
        return {};
    }
    if (pOk) *pOk = true;
    return QString::fromUtf8(f.readAll());
}

bool WriteText(const QString& sPath, const QString& sText)
{
    QSaveFile f(sPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(sText.toUtf8());
    return f.commit();
}

SGameInfo ParseGameInfo(const QString& sText)
{
    SGameInfo info;
    const QStringList aLines = sText.split(QLatin1Char('\n'));
    for (const QString& sLine : aLines) {
        const qsizetype nPos = sLine.indexOf(QLatin1Char('='));
        if (nPos <= 0)
            continue;
        const QString sKey = sLine.left(nPos);
        const QString sValue = sLine.mid(nPos + 1);
        if      (sKey == QLatin1String("Auto_Series_STR"))           info.sSeries = sValue;
        else if (sKey == QLatin1String("Auto_Name_STR"))             info.sName = sValue;
        else if (sKey == QLatin1String("Auto_DevName_STR"))          info.sDevName = sValue;
        else if (sKey == QLatin1String("Auto_System_STR"))           info.sSystem = sValue;
        else if (sKey == QLatin1String("Auto_Genre_STR"))            info.sGenre = sValue;
        else if (sKey == QLatin1String("Auto_FirstReleaseDate_STR")) info.sReleaseDate = sValue;
        else if (sKey == QLatin1String("Private_Comments_STR"))      info.sComments = sValue;
    }
    info.sGenre.remove(QLatin1Char('\t'));
    return info;
}

QString SerializeGameInfo(const SGameInfo& info)
{
    return QStringLiteral("Auto_Series_STR=%1\nAuto_Name_STR=%2\nAuto_DevName_STR=%3\nAuto_System_STR=%4\n"
                          "Auto_Genre_STR=%5\nAuto_FirstReleaseDate_STR=%6\n\nPrivate_Comments_STR=%7\n")
        .arg(info.sSeries, info.sName, info.sDevName, info.sSystem, info.sGenre, info.sReleaseDate, info.sComments);
}

QStringList SortedEntries(const QString& sDir, QDir::Filters filters, const QStringList& aNameFilters = {})
{
    QDir dir(sDir);
    QStringList a = dir.entryList(aNameFilters, filters);
    std::sort(a.begin(), a.end(), [](const QString& s1, const QString& s2) { return LogicalCollator().compare(s1, s2) < 0; });
    return a;
}

// Read one game (GetGameObj)
SGame LoadGame(const QString& sGamePath, const CConfig& config, QStringList* pErrors)
{
    const QFileInfo fiGame(sGamePath);
    const QString sGameFolderName = fiGame.fileName();
    const QString sSystemFolderName = QFileInfo(fiGame.path()).fileName();

    SGame game;
    game.sGamePath = sGamePath;
    game.sThumbMarker = sSystemFolderName + QLatin1Char('_') + sGameFolderName;

    // ---- Read or create GameInfo.txt
    QString sInfoPath = GameLibrary::ChildCaseInsensitive(sGamePath, QStringLiteral("GameInfo.txt"));
    if (sInfoPath.isEmpty())
        sInfoPath = sGamePath + QStringLiteral("/GameInfo.txt");

    bool bOk = false;
    QString sText = ReadText(sInfoPath, &bOk);
    if (!bOk) {
        SGameInfo infoDefault;
        infoDefault.sName = sGameFolderName;
        infoDefault.sSystem = sSystemFolderName;
        sText = SerializeGameInfo(infoDefault);
        if (!WriteText(sInfoPath, sText) && pErrors)
            pErrors->append(QObject::tr("Could not create file: \"%1\"").arg(sInfoPath));
    }

    // ---- Create a placeholder Launch*.conf if none exists
    const QStringList aLaunch = SortedEntries(sGamePath, QDir::Files, { QStringLiteral("Launch*.conf") });
    if (aLaunch.isEmpty()) {
        const QString sPlaceholder = (sSystemFolderName == QLatin1String("PC"))
                                         ? QStringLiteral("path/of/game/binary")
                                         : QStringLiteral("archivename.zip");
        if (!WriteText(sGamePath + QStringLiteral("/Launch.conf"), sPlaceholder) && pErrors)
            pErrors->append(QObject::tr("Could not create file: \"%1\"").arg(sGamePath + QStringLiteral("/Launch.conf")));
    }

    // ---- Normalize: fixed order, known keys only
    game.info = ParseGameInfo(sText);
    const QString sNormalized = SerializeGameInfo(game.info);
    if (sNormalized != sText && !WriteText(sInfoPath, sNormalized) && pErrors)
        pErrors->append(QObject::tr("Could not create file: \"%1\"").arg(sInfoPath));

    // ---- Cover (Cover.jpg takes precedence over Cover.png)
    game.sCoverPath = GameLibrary::ChildCaseInsensitive(sGamePath, QStringLiteral("Cover.jpg"));
    if (game.sCoverPath.isEmpty())
        game.sCoverPath = GameLibrary::ChildCaseInsensitive(sGamePath, QStringLiteral("Cover.png"));

    // ---- Reviews present?
    const QString sReviews = GameLibrary::ChildCaseInsensitive(sGamePath, QStringLiteral("Reviews"));
    game.bHaveReviews = !sReviews.isEmpty() && !QDir(sReviews).entryList(QDir::Files | QDir::Hidden).isEmpty();

    // ---- Installed? (as in the original, only the first *.conf counts)
    const QStringList aConf = GameLibrary::ConfFiles(sGamePath);
    if (!aConf.isEmpty())
        game.bInstalled = GameLibrary::GetLaunchData(aConf.first(), config).bAvailable;

    return game;
}

} // namespace

QString FieldKey(EField eField)
{
    switch (eField) {
        case EField::None:          return QStringLiteral("|");
        case EField::Series:        return QStringLiteral("Auto_Series_STR");
        case EField::Name:          return QStringLiteral("Auto_Name_STR");
        case EField::DevName:       return QStringLiteral("Auto_DevName_STR");
        case EField::System:        return QStringLiteral("Auto_System_STR");
        case EField::Genre:         return QStringLiteral("Auto_Genre_STR");
        case EField::ReleaseDate:   return QStringLiteral("Auto_FirstReleaseDate_STR");
        case EField::ReleaseDateYM: return QStringLiteral("Auto_FirstReleaseDateYM_STR");
    }
    return {};
}

EField FieldFromKey(const QString& sKey, EField eFallback)
{
    for (EField e : { EField::None, EField::Series, EField::Name, EField::DevName, EField::System,
                      EField::Genre, EField::ReleaseDate, EField::ReleaseDateYM }) {
        if (FieldKey(e) == sKey)
            return e;
    }
    return eFallback;
}

QString SGame::Field(EField eField) const
{
    switch (eField) {
        case EField::None:          return {};
        case EField::Series:        return info.sSeries;
        case EField::Name:          return info.sName;
        case EField::DevName:       return info.sDevName;
        case EField::System:        return info.sSystem;
        case EField::Genre:         return info.sGenre;
        case EField::ReleaseDate:
        case EField::ReleaseDateYM: return info.sReleaseDate;
    }
    return {};
}

namespace GameLibrary {

QList<SGame> Load(const CConfig& config, QStringList* pErrors)
{
    // Collect all game folders and sort them naturally by full path, as in the original
    QStringList aFolders;
    for (const QString& sSystem : SortedEntries(config.sGameInfoDir, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString sSystemPath = config.sGameInfoDir + QLatin1Char('/') + sSystem;
        for (const QString& sGame : SortedEntries(sSystemPath, QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (sGame.startsWith(QLatin1Char('_')))     // ignore folders starting with "_"
                continue;
            aFolders.append(sSystemPath + QLatin1Char('/') + sGame);
        }
    }
    std::sort(aFolders.begin(), aFolders.end(), [](const QString& s1, const QString& s2) { return CompareLogical(s1, s2) < 0; });

    QList<SGame> aGames;
    aGames.reserve(aFolders.size());
    for (const QString& sFolder : std::as_const(aFolders))
        aGames.append(LoadGame(sFolder, config, pErrors));
    return aGames;
}

SGameInfo ReadGameInfo(const QString& sGamePath)
{
    const QString sPath = ChildCaseInsensitive(sGamePath, QStringLiteral("GameInfo.txt"));
    return sPath.isEmpty() ? SGameInfo{} : ParseGameInfo(ReadText(sPath));
}

SLaunchData GetLaunchData(const QString& sConfPath, const CConfig& config)
{
    SLaunchData launch;
    QString sConfText = ReadText(sConfPath).trimmed();
    sConfText = sConfText.section(QLatin1Char('\n'), 0, 0).trimmed();

    // <GameInfo>/<System>/<Game>/<x>.conf -> system folder
    const QString sSystemFolderName = QFileInfo(QFileInfo(QFileInfo(sConfPath).path()).path()).fileName();

    if (sSystemFolderName == QLatin1String("PC")) {
        // Native Linux programs, launched directly. Launch.conf contains an absolute path
        // (also "~/...") or a path relative to the PC games folder (PcGamesDir).
        QString sBinary = sConfText;
        if (sBinary.startsWith(QStringLiteral("~/")))
            sBinary = QDir::homePath() + sBinary.mid(1);
        else if (QFileInfo(sBinary).isRelative())
            sBinary = config.sPcGamesDir + QLatin1Char('/') + sBinary;
        sBinary = QDir::cleanPath(sBinary);

        const QFileInfo fiBinary(sBinary);
        launch.bAvailable = fiBinary.isFile() && fiBinary.isExecutable();
        if (!fiBinary.isFile())
            launch.sMissing = sBinary;
        else if (!fiBinary.isExecutable())
            launch.sMissing = QObject::tr("%1 (not executable)").arg(sBinary);
        launch.sProgram = sBinary;
        launch.sWorkingDir = fiBinary.path();   // working dir = program folder (as in the original)
        return launch;
    }

    const SSystemLaunch sys = config.mapSystems.value(sSystemFolderName);
    const QString sCore = config.FindCore(sys.sCore);
    const QString sRom = sys.sRomSubdir.isEmpty() ? QString()
                                                  : config.sRomsDir + QLatin1Char('/') + sys.sRomSubdir + QLatin1Char('/') + sConfText;
    const QString sRetroArch = QFileInfo(config.sRetroArch).isAbsolute() ? config.sRetroArch
                                                                         : QStandardPaths::findExecutable(config.sRetroArch);

    QStringList aMissing;
    if (sRetroArch.isEmpty() || !QFileInfo(sRetroArch).isFile()) aMissing << config.sRetroArch;
    if (!QFileInfo(config.sRetroArchConfig).isFile())            aMissing << config.sRetroArchConfig;
    if (sCore.isEmpty())                                          aMissing << (sys.sCore.isEmpty() ? QObject::tr("core for system \"%1\"").arg(sSystemFolderName) : sys.sCore + QStringLiteral(".so"));
    if (sRom.isEmpty() || !QFileInfo(sRom).isFile())              aMissing << (sRom.isEmpty() ? QObject::tr("ROM folder for system \"%1\"").arg(sSystemFolderName) : sRom);

    launch.bAvailable  = aMissing.isEmpty();
    launch.sMissing    = aMissing.join(QLatin1Char('\n'));
    launch.sProgram    = sRetroArch.isEmpty() ? config.sRetroArch : sRetroArch;
    launch.aArgs       = { QStringLiteral("-c"), config.sRetroArchConfig, QStringLiteral("-L"), sCore, sRom };
    launch.sWorkingDir = QFileInfo(launch.sProgram).path();
    return launch;
}

QStringList ConfFiles(const QString& sGamePath)
{
    QStringList a;
    for (const QString& sName : SortedEntries(sGamePath, QDir::Files, { QStringLiteral("*.conf") }))
        a.append(sGamePath + QLatin1Char('/') + sName);
    return a;
}

QStringList FilesIn(const QString& sDir)
{
    QStringList a;
    if (sDir.isEmpty())
        return a;
    for (const QString& sName : SortedEntries(sDir, QDir::Files))
        a.append(sDir + QLatin1Char('/') + sName);
    return a;
}

QString ChildCaseInsensitive(const QString& sDir, const QString& sName)
{
    // On Windows, file names are case-insensitive (e.g. "cover.png" instead of "Cover.png")
    const QString sExact = sDir + QLatin1Char('/') + sName;
    if (QFileInfo::exists(sExact))
        return sExact;
    const QStringList aEntries = QDir(sDir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString& sEntry : aEntries) {
        if (sEntry.compare(sName, Qt::CaseInsensitive) == 0)
            return sDir + QLatin1Char('/') + sEntry;
    }
    return {};
}

QString Unserialize(QString s)
{
    // Order as in the original
    s.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    s.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
    s.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    s.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    return s;
}

int CompareLogical(const QString& s1, const QString& s2)
{
    return LogicalCollator().compare(s1, s2);
}

} // namespace GameLibrary
