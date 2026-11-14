#include "Config.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString ReadOrDefault(QSettings& settings, const QString& sKey, const QString& sDefault)
{
    if (!settings.contains(sKey)) {
        settings.setValue(sKey, sDefault);
        return sDefault;
    }
    return settings.value(sKey).toString();
}

// In the original, GameInfo, ROMs and PC games all live under g_FolderUserD:
//   <UserD>/GameBrowser/GameInfo, <UserD>/retroarch-roms, <UserD>/Games
QString UserDirFromGameInfo(const QString& sGameInfoDir)
{
    if (sGameInfoDir.isEmpty())
        return {};
    return QDir::cleanPath(sGameInfoDir + QStringLiteral("/../.."));
}

} // namespace

void CConfig::Load(QSettings& settings)
{
    sGameInfoDir = settings.value(QStringLiteral("Paths/GameInfo")).toString();
    const QString sUserD = UserDirFromGameInfo(sGameInfoDir);
    const QString sHome = QDir::homePath();

    sThumbsCacheDir = ReadOrDefault(settings, QStringLiteral("Paths/ThumbsCache"),
                                    QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

    QString sDefaultRoms = sUserD + QStringLiteral("/retroarch-roms");
    if (sUserD.isEmpty() || !QFileInfo(sDefaultRoms).isDir())
        sDefaultRoms = sHome + QStringLiteral("/.local/share/retroarch-roms");

    sRetroArch       = ReadOrDefault(settings, QStringLiteral("Paths/RetroArch"), QStringLiteral("retroarch"));
    sRetroArchConfig = ReadOrDefault(settings, QStringLiteral("Paths/RetroArchConfig"), sHome + QStringLiteral("/.config/retroarch/retroarch.cfg"));
    aCoreDirs        = ReadOrDefault(settings, QStringLiteral("Paths/CoreDirs"), sHome + QStringLiteral("/.config/retroarch/cores;/usr/lib/libretro")).split(QLatin1Char(';'), Qt::SkipEmptyParts);

    // ROM and PC folders: empty entry = derive automatically from the GameInfo folder (as in the original).
    // Derived values are not saved, so they follow a changed GameInfo folder.
    sRomsDir         = ReadOrDefault(settings, QStringLiteral("Paths/RomsDir"), QString());
    sPcGamesDir      = ReadOrDefault(settings, QStringLiteral("Paths/PcGamesDir"), QString());

    if (sRomsDir.isEmpty()) {
        sRomsDir = sDefaultRoms;
    }

    if (sPcGamesDir.isEmpty() && !sUserD.isEmpty()) {
        sPcGamesDir = sUserD + QStringLiteral("/Games");
    }

    // System table from GetLaunchData() (cores as Linux .so instead of Windows .dll)
    const QList<QPair<QString, SSystemLaunch>> aDefaults = {
        { "Arcade",             { "mame_libretro",           "MAME" } },
        { "Neo Geo",            { "mame_libretro",           "SNK - Neo Geo" } },
        { "Sega Master System", { "genesis_plus_gx_libretro", "Sega - Master System - Mark III" } },
        { "Sega Mega Drive",    { "genesis_plus_gx_libretro", "Sega - Mega Drive - Genesis" } },
        { "Sony PlayStation",   { "mednafen_psx_libretro",   "Sony - PlayStation" } },
        { "Nintendo SNES",      { "bsnes_libretro",          "Nintendo - Super Nintendo Entertainment System" } },
    };

    settings.beginGroup(QStringLiteral("Systems"));
    if (settings.childGroups().isEmpty()) {
        for (const auto& [sSystem, sys] : aDefaults) {
            settings.setValue(sSystem + QStringLiteral("/Core"), sys.sCore);
            settings.setValue(sSystem + QStringLiteral("/RomSubdir"), sys.sRomSubdir);
        }
    }

    mapSystems.clear();
    for (const QString& sSystem : settings.childGroups()) {
        SSystemLaunch sys;
        sys.sCore      = settings.value(sSystem + QStringLiteral("/Core")).toString();
        sys.sRomSubdir = settings.value(sSystem + QStringLiteral("/RomSubdir")).toString();
        mapSystems.insert(sSystem, sys);
    }
    settings.endGroup();
}

bool CConfig::HasValidGameInfoDir() const
{
    return !sGameInfoDir.isEmpty() && QFileInfo(sGameInfoDir).isDir();
}

void CConfig::SetGameInfoDir(QSettings& settings, const QString& sDir)
{
    settings.setValue(QStringLiteral("Paths/GameInfo"), QDir::cleanPath(sDir));
    Load(settings);
}

QString CConfig::FindCore(const QString& sCore) const
{
    if (sCore.isEmpty())
        return {};
    for (const QString& sDir : aCoreDirs) {
        const QString sPath = sDir + QLatin1Char('/') + sCore + QStringLiteral(".so");
        if (QFileInfo(sPath).isFile())
            return sPath;
    }
    return {};
}
