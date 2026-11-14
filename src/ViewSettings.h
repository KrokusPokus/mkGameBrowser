#ifndef VIEWSETTINGS_H
#define VIEWSETTINGS_H

#include "GameLibrary.h"

#include <QList>
#include <QString>
#include <QStringList>

// Hide menu (g_sCLFilter). Saved in the same format as in the AHK script:
// "|HideUninstalled|HideReviewed|OnlyShowGenre_Shmup|OnlyShowSystem_Neo Geo|"
struct SFilter {
    bool        bHideUninstalled = false;
    bool        bHideInstalled   = false;
    bool        bHideReviewed    = false;
    bool        bHideUnreviewed  = false;
    QStringList aOnlyGenres;        // "All but Genre": show only games of these genres
    QStringList aOnlySystems;       // "All but Platform"

    bool IsEmpty() const;
    QString Serialize() const;
    static SFilter Parse(const QString& s);
};

// Shade menu (g_sCLShader): affected games are drawn grey/semi-transparent
struct SShade {
    bool bShadeUninstalled = true;  // default of the original: "|ShadeUninstalled|"
    bool bShadeInstalled   = false;
    bool bShadeReviewed    = false;
    bool bShadeUnreviewed  = false;

    bool IsEmpty() const;
    QString Serialize() const;
    static SShade Parse(const QString& s);
};

struct SViewSettings {
    EField  eGroup = EField::System;        // default: Auto_System_STR
    EField  eSort  = EField::ReleaseDate;   // default: Auto_FirstReleaseDate_STR
    EField  eText  = EField::Name;          // default: Auto_Name_STR
    SFilter filter;
    SShade  shade;
    QString sTextFilter;                    // filter bar (Ctrl+F), not saved
};

// A group in the window (g_GroupingList_OBJ)
struct SGroup {
    QString     sTitle;
    QList<int>  aMembers;           // indices into the game list (a game can be in several groups)
    int         nHidden = 0;
};

namespace ViewLogic {

// Sets bVisible/bShaded of each game (filter part of GetFullList())
void ApplyFilter(QList<SGame>& aGames, const SViewSettings& settings);

// Groups and sorts (steps 3-5 of the original)
QList<SGroup> BuildGroups(const QList<SGame>& aGames, const SViewSettings& settings);

// All occurring genres or systems (for the "Hide" submenus), sorted
QStringList DistinctValues(const QList<SGame>& aGames, EField eField);

} // namespace ViewLogic

#endif // VIEWSETTINGS_H
