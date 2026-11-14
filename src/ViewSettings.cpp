#include "ViewSettings.h"

#include <QCollator>

#include <algorithm>

namespace {

const QString kGenrePrefix  = QStringLiteral("OnlyShowGenre_");
const QString kSystemPrefix = QStringLiteral("OnlyShowSystem_");

QStringList Tokens(const QString& s)
{
    return s.split(QLatin1Char('|'), Qt::SkipEmptyParts);
}

QString Join(const QStringList& aTokens)
{
    return aTokens.isEmpty() ? QStringLiteral("|") : QLatin1Char('|') + aTokens.join(QLatin1Char('|')) + QLatin1Char('|');
}

// When grouping, platforms are sorted chronologically (by European release) instead of alphabetically
QString SystemSortKey(const QString& sSystem)
{
    static const QList<QPair<QString, QString>> s_aOrder = {
        { "Nintendo NES",       "(1986-09) " },
        { "Sega Master System", "(1990-12) " },
        { "Sega Mega Drive",    "(1991-10) " },
        { "Nintendo SNES",      "(1993-03) " },
        { "Neo Geo",            "(1995-03) " },
        { "Sony PlayStation",   "(1998-00) " },
        { "Sega Dreamcast",     "(2000-09) " },
        { "Sony PS3",           "(2010-08) " },
    };
    for (const auto& [sName, sPrefix] : s_aOrder) {
        if (sSystem.compare(sName, Qt::CaseInsensitive) == 0)
            return sPrefix + sSystem;
    }
    return sSystem;
}

// Pad the date to full length so that "1991" is sorted after "1991-12-31"
QString PadDate(const QString& sDate, const QString& sMask)
{
    return sDate.size() >= sMask.size() ? sDate : sDate + sMask.mid(sDate.size());
}

} // namespace

// ============================================================================================ SFilter

bool SFilter::IsEmpty() const
{
    return !bHideUninstalled && !bHideInstalled && !bHideReviewed && !bHideUnreviewed
           && aOnlyGenres.isEmpty() && aOnlySystems.isEmpty();
}

QString SFilter::Serialize() const
{
    QStringList a;
    if (bHideUninstalled) a << QStringLiteral("HideUninstalled");
    if (bHideInstalled)   a << QStringLiteral("HideInstalled");
    if (bHideReviewed)    a << QStringLiteral("HideReviewed");
    if (bHideUnreviewed)  a << QStringLiteral("HideUnreviewed");
    for (const QString& s : aOnlyGenres)  a << kGenrePrefix + s;
    for (const QString& s : aOnlySystems) a << kSystemPrefix + s;
    return Join(a);
}

SFilter SFilter::Parse(const QString& s)
{
    SFilter f;
    for (const QString& sToken : Tokens(s)) {
        if      (sToken == QLatin1String("HideUninstalled")) f.bHideUninstalled = true;
        else if (sToken == QLatin1String("HideInstalled"))   f.bHideInstalled = true;
        else if (sToken == QLatin1String("HideReviewed"))    f.bHideReviewed = true;
        else if (sToken == QLatin1String("HideUnreviewed"))  f.bHideUnreviewed = true;
        else if (sToken.startsWith(kGenrePrefix))            f.aOnlyGenres << sToken.mid(kGenrePrefix.size());
        else if (sToken.startsWith(kSystemPrefix))           f.aOnlySystems << sToken.mid(kSystemPrefix.size());
    }
    return f;
}

// ============================================================================================ SShade

bool SShade::IsEmpty() const
{
    return !bShadeUninstalled && !bShadeInstalled && !bShadeReviewed && !bShadeUnreviewed;
}

QString SShade::Serialize() const
{
    QStringList a;
    if (bShadeUninstalled) a << QStringLiteral("ShadeUninstalled");
    if (bShadeInstalled)   a << QStringLiteral("ShadeInstalled");
    if (bShadeReviewed)    a << QStringLiteral("ShadeReviewed");
    if (bShadeUnreviewed)  a << QStringLiteral("ShadeUnreviewed");
    return Join(a);
}

SShade SShade::Parse(const QString& s)
{
    SShade sh;
    sh.bShadeUninstalled = false;
    for (const QString& sToken : Tokens(s)) {
        if      (sToken == QLatin1String("ShadeUninstalled")) sh.bShadeUninstalled = true;
        else if (sToken == QLatin1String("ShadeInstalled"))   sh.bShadeInstalled = true;
        else if (sToken == QLatin1String("ShadeReviewed"))    sh.bShadeReviewed = true;
        else if (sToken == QLatin1String("ShadeUnreviewed"))  sh.bShadeUnreviewed = true;
    }
    return sh;
}

// ============================================================================================ ViewLogic

namespace ViewLogic {

void ApplyFilter(QList<SGame>& aGames, const SViewSettings& settings)
{
    const SFilter& f = settings.filter;
    const SShade& sh = settings.shade;
    // Filter bar: every word must occur (as in mkFolderWidget); all displayed fields are searched
    const QStringList aWords = settings.sTextFilter.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    for (SGame& game : aGames) {
        bool bShow = true;
        bool bShade = false;

        if (!game.bInstalled) {
            if (f.bHideUninstalled)       bShow = false;
            else if (sh.bShadeUninstalled) bShade = true;
        }
        else {
            if (f.bHideInstalled)         bShow = false;
            else if (sh.bShadeInstalled)   bShade = true;
        }

        if (game.bHaveReviews) {
            if (f.bHideReviewed)          bShow = false;
            else if (sh.bShadeReviewed)    bShade = true;
        }
        else {
            if (f.bHideUnreviewed)        bShow = false;
            else if (sh.bShadeUnreviewed)  bShade = true;
        }

        // "All but ...": games without a value stay visible, as in the original
        auto matchesAny = [](const QString& sValues, const QStringList& aAllowed) {
            for (const QString& s : sValues.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
                if (aAllowed.contains(s, Qt::CaseInsensitive))
                    return true;
            }
            return false;
        };
        if (!f.aOnlySystems.isEmpty() && !game.info.sSystem.isEmpty() && !matchesAny(game.info.sSystem, f.aOnlySystems))
            bShow = false;
        if (!f.aOnlyGenres.isEmpty() && !game.info.sGenre.isEmpty() && !matchesAny(game.info.sGenre, f.aOnlyGenres))
            bShow = false;

        if (bShow && !aWords.isEmpty()) {
            const QString sHaystack = QStringList{ GameLibrary::Unserialize(game.info.sName), game.info.sSeries,
                                                   game.info.sDevName, game.info.sSystem, game.info.sGenre,
                                                   game.info.sReleaseDate }.join(QLatin1Char('\n'));
            for (const QString& sWord : aWords) {
                if (!sHaystack.contains(sWord, Qt::CaseInsensitive)) {
                    bShow = false;
                    break;
                }
            }
        }

        game.bVisible = bShow;
        game.bShaded = bShade;
    }
}

QList<SGroup> BuildGroups(const QList<SGame>& aGames, const SViewSettings& settings)
{
    const EField eGroup = settings.eGroup;
    const EField eSort  = settings.eSort;
    const QString sUnspecified = QStringLiteral("Unspecified");

    struct SEntry {
        QString sGroupKey;      // SortObject1
        QString sSortKey;       // SortObject2
        QString sHeader;
        int     nGame;
    };
    QList<SEntry> aEntries;

    for (int i = 0; i < aGames.size(); ++i) {
        const SGame& game = aGames.at(i);
        const QString sRaw = game.Field(eGroup);

        // ---- Sort key of the group (SortObject1)
        QString sGroupKey;
        if (eGroup == EField::Name)
            sGroupKey = sRaw.left(1);                                  // first letter only
        else if (eGroup == EField::ReleaseDate)
            sGroupKey = sRaw.left(4);
        else if (eGroup == EField::ReleaseDateYM)
            sGroupKey = sRaw.left(3).compare(QLatin1String("tba"), Qt::CaseInsensitive) == 0 ? sRaw.left(7)
                                                                                              : PadDate(sRaw.left(7), QStringLiteral("xxxx-xx"));
        else if (eGroup == EField::System)
            sGroupKey = SystemSortKey(sRaw);
        else
            sGroupKey = sRaw;

        // ---- Sort key within the group (SortObject2)
        QString sSortKey = (eSort == EField::ReleaseDate || eSort == EField::ReleaseDateYM)
                               ? PadDate(game.info.sReleaseDate, QStringLiteral("xxxx-xx-xx"))
                               : game.Field(eSort);

        if (sGroupKey.isEmpty()) sGroupKey = sUnspecified;
        if (sSortKey.isEmpty())  sSortKey = sUnspecified;

        // ---- Header
        auto headerFor = [&](const QString& sPart) -> QString {
            if (sRaw.isEmpty())
                return sUnspecified;
            if (sRaw.contains(QLatin1Char('|')))                       // multiple values -> respective part
                return sPart;
            switch (eGroup) {
                case EField::ReleaseDate:   return sRaw.left(4);
                case EField::ReleaseDateYM: return sRaw.left(7);
                case EField::Name:          return sRaw.left(1);
                default:                    return sRaw;
            }
        };

        // Multiple values ("Shmup|Action") -> game appears in each of these groups
        if (sGroupKey.contains(QLatin1Char('|'))) {
            for (const QString& sPart : sGroupKey.split(QLatin1Char('|'), Qt::SkipEmptyParts))
                aEntries.append({ sPart, sSortKey, headerFor(sPart), i });
        }
        else {
            aEntries.append({ sGroupKey, sSortKey, headerFor(sGroupKey), i });
        }
    }

    // Like "Sort,...,FFileSortLogical" on "Group<TAB>Sort<TAB>Index"
    std::stable_sort(aEntries.begin(), aEntries.end(), [](const SEntry& a, const SEntry& b) {
        if (const int n = GameLibrary::CompareLogical(a.sGroupKey, b.sGroupKey); n != 0)
            return n < 0;
        if (const int n = GameLibrary::CompareLogical(a.sSortKey, b.sSortKey); n != 0)
            return n < 0;
        return a.nGame < b.nGame;
    });

    // New group as soon as the header changes (case-insensitive comparison as in AHK)
    QList<SGroup> aGroups;
    for (const SEntry& e : std::as_const(aEntries)) {
        if (aGroups.isEmpty() || aGroups.last().sTitle.compare(e.sHeader, Qt::CaseInsensitive) != 0)
            aGroups.append({ e.sHeader, {}, 0 });
        aGroups.last().aMembers.append(e.nGame);
        if (!aGames.at(e.nGame).bVisible)
            ++aGroups.last().nHidden;
    }
    return aGroups;
}

QStringList DistinctValues(const QList<SGame>& aGames, EField eField)
{
    QStringList a;
    for (const SGame& game : aGames) {
        for (const QString& s : game.Field(eField).split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
            if (!a.contains(s, Qt::CaseInsensitive))
                a.append(s);
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(a.begin(), a.end(), [&collator](const QString& s1, const QString& s2) { return collator.compare(s1, s2) < 0; });
    return a;
}

} // namespace ViewLogic
