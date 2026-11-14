#include "MainWindow.h"

#include "GameGridView.h"
#include "Helpers.h"
#include "ThumbnailCache.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kThumbHeight = 172;

QIcon IconForFile(const QString& sPath)
{
    static QMimeDatabase s_db;
    const QMimeType mime = s_db.mimeTypeForFile(sPath);
    QIcon icon = QIcon::fromTheme(mime.iconName());
    if (icon.isNull())
        icon = QIcon::fromTheme(mime.genericIconName(), QIcon::fromTheme(QStringLiteral("text-x-generic")));
    return icon;
}

} // namespace

CMainWindow::CMainWindow(QSettings& settings, const CConfig& config, QWidget* pParent)
    : QMainWindow(pParent)
    , m_settings(settings)
    , m_config(config)
{
    InitStateFile();

    // ---- saved view (registry values of the original)
    if (m_pState) {
        m_pState->beginGroup(QStringLiteral("View"));
        m_view.eGroup = FieldFromKey(m_pState->value(QStringLiteral("GroupMode")).toString(), EField::System);
        m_view.eSort  = FieldFromKey(m_pState->value(QStringLiteral("SortMode")).toString(), EField::ReleaseDate);
        m_view.eText  = FieldFromKey(m_pState->value(QStringLiteral("TextMode")).toString(), EField::Name);
        m_view.filter = SFilter::Parse(m_pState->value(QStringLiteral("FilterMode"), QStringLiteral("|")).toString());
        m_view.shade  = SShade::Parse(m_pState->value(QStringLiteral("ShaderMode"), QStringLiteral("|ShadeUninstalled|")).toString());
        m_pState->endGroup();
    }
    if (m_view.eGroup == EField::None)
        m_view.eGroup = EField::System;
    if (m_view.eSort == EField::None || m_view.eSort == EField::ReleaseDateYM)
        m_view.eSort = EField::ReleaseDate;

    // ---- Layout as in mkFolderWidget: filter bar (initially hidden) above the view
    auto* pCentralWidget = new QWidget(this);
    setCentralWidget(pCentralWidget);
    auto* pMainLayout = new QVBoxLayout(pCentralWidget);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    pMainLayout->setSpacing(0);

    m_pTopControlsContainerWidget = new QWidget();
    auto* pTopControlsHBoxLayout = new QHBoxLayout(m_pTopControlsContainerWidget);
    pTopControlsHBoxLayout->setContentsMargins(0, 0, 0, 0);
    pTopControlsHBoxLayout->setSpacing(0);

    m_pFilterEdit = new QLineEdit();
    pTopControlsHBoxLayout->addWidget(m_pFilterEdit);

    pMainLayout->addWidget(m_pTopControlsContainerWidget);
    m_pTopControlsContainerWidget->hide();

    m_pView = new CGameGridView(this);
    pMainLayout->addWidget(m_pView);

    connect(m_pFilterEdit, &QLineEdit::textChanged, this, &CMainWindow::OnFilterBoxChange);
    UpdateWidgetStyles();
    qApp->installEventFilter(this);

    connect(m_pView, &CGameGridView::LaunchRequested, this, [this](int nGame) {
        const QString sConf = GameLibrary::ChildCaseInsensitive(m_aGames.at(nGame).sGamePath, QStringLiteral("Launch.conf"));
        if (sConf.isEmpty())
            QMessageBox::warning(this, windowTitle(), tr("Can't find game config file:\n\t\"%1\"").arg(m_aGames.at(nGame).sGamePath + QStringLiteral("/Launch.conf")));
        else
            Launch(sConf);
    });
    connect(m_pView, &CGameGridView::ContextMenuRequested, this, [this](int nGame, const QPoint& ptGlobal) {
        if (nGame < 0)
            ShowBackgroundMenu(ptGlobal);
        else
            ShowGameMenu(nGame, ptGlobal);
    });
    connect(m_pView, &CGameGridView::EditGameInfoRequested, this, [this](int nGame) {
        const QString sPath = GameLibrary::ChildCaseInsensitive(m_aGames.at(nGame).sGamePath, QStringLiteral("GameInfo.txt"));
        if (!sPath.isEmpty())
            EditFile(sPath);
    });
    connect(m_pView, &CGameGridView::WebSearchRequested, this, &CMainWindow::WebSearch);
    connect(m_pView, &CGameGridView::OpenFolderRequested, this, &CMainWindow::OpenFolder);
    connect(m_pView, &CGameGridView::ReloadRequested, this, &CMainWindow::LoadLibrary);
    connect(m_pView, &CGameGridView::HelpRequested, this, &CMainWindow::ShowHelp);

    RestoreWindow();
}

CMainWindow::~CMainWindow()
{
    qApp->removeEventFilter(this);
}

// =============================================================================================== Loading / display

void CMainWindow::LoadLibrary()
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const QPoint ptScroll = m_pView->ScrollPosition();

    // Thumbnail cache matching the current screen scaling (original: ThumbsCache<DPI>).
    // Only recreate it when the scaling changed; otherwise it is enough to discard the images in memory
    // (the cache detects changed covers itself by size and modification time).
    const qreal dDpr = devicePixelRatioF();
    if (m_pCache && qFuzzyCompare(m_pCache->DevicePixelRatio(), dDpr)) {
        m_pCache->ClearMemory();
    }
    else {
        m_pView->SetContent(nullptr, {}, m_view.eText, nullptr);   // view must no longer use the old cache
        if (m_pCache)
            m_pCache->SaveIndex();
        delete m_pCache;
        m_pCache = new CThumbnailCache(m_config.sThumbsCacheDir, kThumbHeight, dDpr, this);
        connect(m_pCache, &CThumbnailCache::ThumbnailFailed, this, &CMainWindow::OnThumbnailFailed);
    }

    QStringList aErrors;
    m_aGames = GameLibrary::Load(m_config, &aErrors);
    m_aGenres = ViewLogic::DistinctValues(m_aGames, EField::Genre);
    m_aSystems = ViewLogic::DistinctValues(m_aGames, EField::System);

    const bool bFirstLoad = m_bFirstShow;   // ApplyView() restores the saved position the first time
    ApplyView(false);
    if (!bFirstLoad)
        m_pView->SetScrollPosition(ptScroll);
    m_pCache->SaveIndex();
    QGuiApplication::restoreOverrideCursor();

    if (!aErrors.isEmpty())
        QMessageBox::warning(this, tr("Game Browser"), aErrors.join(QLatin1Char('\n')));

    // Empty library: usually a wrong folder -> explain and let the user choose another one
    if (m_aGames.isEmpty()) {
        const auto eAnswer = QMessageBox::question(this, tr("Game Browser"),
            tr("No games found in\n\t%1\n\nThe GameInfo folder must contain one subfolder per platform, "
               "each with one subfolder per game (e.g. \"GameInfo/Neo Geo/Pulstar\").\n\nSelect a different folder?")
                .arg(m_config.sGameInfoDir),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (eAnswer != QMessageBox::Yes)
            return;
        const QString sDir = QFileDialog::getExistingDirectory(this, tr("Select the GameInfo folder"), m_config.sGameInfoDir);
        if (sDir.isEmpty())
            return;
        m_config.SetGameInfoDir(m_settings, sDir);
        m_settings.sync();
        QTimer::singleShot(0, this, &CMainWindow::LoadLibrary);
    }
}

void CMainWindow::ApplyView(bool bRestoreScroll)
{
    const QPoint ptScroll = m_pView->ScrollPosition();
    ViewLogic::ApplyFilter(m_aGames, m_view);
    m_aGroups = ViewLogic::BuildGroups(m_aGames, m_view);
    m_pView->SetContent(&m_aGames, m_aGroups, m_view.eText, m_pCache);

    if (m_bFirstShow) {
        m_bFirstShow = false;
        if (m_pState) {
            m_pState->beginGroup(QStringLiteral("Window"));
            const QPoint ptSaved(m_pState->value(QStringLiteral("ScrollX"), 0).toInt(), m_pState->value(QStringLiteral("ScrollY"), 0).toInt());
            m_pState->endGroup();
            m_pView->SetScrollPosition(ptSaved);
        }
    }
    else if (bRestoreScroll) {
        m_pView->SetScrollPosition(ptScroll);
    }
    UpdateTitle();
}

// =============================================================================================== Filter bar

void CMainWindow::ShowFilterBox()
{
    if (m_pTopControlsContainerWidget->isHidden())
        m_pTopControlsContainerWidget->show();
    if (!m_pFilterEdit->hasFocus())
        m_pFilterEdit->setFocus();
    m_pFilterEdit->selectAll();
}

void CMainWindow::HideFilterBox()
{
    m_pFilterEdit->clear();
    m_pTopControlsContainerWidget->hide();
    m_pView->setFocus();
}

void CMainWindow::OnFilterBoxChange(const QString& sText)
{
    if (sText == m_view.sTextFilter)
        return;
    m_view.sTextFilter = sText;
    ApplyView(false);
    m_pView->SetScrollPosition({});     // matches are at the top
}

void CMainWindow::UpdateWidgetStyles()
{
    // Same look as the filter bar in mkFolderWidget
    static const QString lineEditStyleDark = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid #555555;"
        "  padding: 0px;"
        "  padding-left: 4px;"
        "  background-color: #1a1a1a;"
        "  color: #ffffff;"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid #666666;"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
        "}"
        );

    static const QString lineEditStyleLight = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  padding: 0px;"
        "  padding-left: 4px;"
        "  background-color: palette(window);"
        "  color: palette(window-text);"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid #ff0000;"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
        "}"
        );

    const bool bDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    if (m_bStyleSet && bDark == m_bStyleDark)
        return;
    m_bStyleSet = true;
    m_bStyleDark = bDark;
    m_pFilterEdit->setStyleSheet(bDark ? lineEditStyleDark : lineEditStyleLight);
}

void CMainWindow::changeEvent(QEvent* pEvent)
{
    switch (pEvent->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::ThemeChange:
            m_bStyleSet = false;    // palette (accent color) can also change without a light/dark switch
            UpdateWidgetStyles();
            break;
        default:
            break;
    }
    QMainWindow::changeEvent(pEvent);
}

// Installed on qApp (as in mkFolderWidget)
bool CMainWindow::eventFilter(QObject* pObj, QEvent* pEvent)
{
    if (pEvent->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(pObj, pEvent);

    auto* pKeyEvent = static_cast<QKeyEvent*>(pEvent);
    auto* pTargetWidget = qobject_cast<QWidget*>(pObj);
    if (!pTargetWidget || pTargetWidget->window() != this)
        return QMainWindow::eventFilter(pObj, pEvent);     // not our window

    // Window-wide: Ctrl+F opens the filter bar
    if (pKeyEvent->modifiers() == Qt::ControlModifier && pKeyEvent->key() == Qt::Key_F) {
        ShowFilterBox();
        return true;
    }

    // Keys in the filter bar
    if (pTargetWidget == m_pFilterEdit) {
        if (pKeyEvent->key() == Qt::Key_Escape) {
            HideFilterBox();
            return true;
        }
        if (pKeyEvent->key() == Qt::Key_Backspace && pKeyEvent->modifiers() == Qt::ControlModifier) {
            if (m_pFilterEdit->text().isEmpty())
                HideFilterBox();
            else
                m_pFilterEdit->clear();
            return true;
        }
        return QMainWindow::eventFilter(pObj, pEvent);
    }

    // Keys in the view
    if (pTargetWidget == m_pView || pTargetWidget->parent() == m_pView) {
        if (pKeyEvent->key() == Qt::Key_Backspace) {
            if (!m_pTopControlsContainerWidget->isHidden()) {
                m_pFilterEdit->setFocus();
                if (pKeyEvent->modifiers() == Qt::ControlModifier)
                    m_pFilterEdit->selectAll();
            }
            return true;
        }
        if (pKeyEvent->key() == Qt::Key_Escape && pKeyEvent->modifiers() == Qt::NoModifier) {
            // Close an open filter bar first, otherwise the window (GuiEscape in the original)
            if (!m_pTopControlsContainerWidget->isHidden())
                HideFilterBox();
            else
                close();
            return true;
        }
    }
    return QMainWindow::eventFilter(pObj, pEvent);
}

void CMainWindow::UpdateTitle()
{
    int nInstalled = 0;
    int nShown = 0;
    for (const SGame& game : std::as_const(m_aGames)) {
        nInstalled += game.bInstalled ? 1 : 0;
        nShown += game.bVisible ? 1 : 0;
    }
    if (m_view.filter.IsEmpty() && m_view.sTextFilter.trimmed().isEmpty())
        setWindowTitle(tr("All Games (%1 total / %2 installed)").arg(m_aGames.size()).arg(nInstalled));
    else
        setWindowTitle(tr("Partial List: %1 of %2 Games shown").arg(nShown).arg(m_aGames.size()));
}

// =============================================================================================== Settings

void CMainWindow::InitStateFile()
{
    // State (view, window geometry, scroll position) separate from the configuration:
    // ~/.local/state/mkGameBrowser/state.ini
    const QString statePath = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    const QDir stateDir(statePath);
    if (!stateDir.exists()) {
        if (!stateDir.mkpath(QStringLiteral("."))) { // creates the directory including subfolders
            return;
        }
    }

    m_stateFileFullPath = QDir(statePath).filePath(QStringLiteral("state.ini"));
    m_pState = std::make_unique<QSettings>(m_stateFileFullPath, QSettings::IniFormat);
}

void CMainWindow::RestoreWindow()
{
    QByteArray baGeometry;
    if (m_pState) {
        m_pState->beginGroup(QStringLiteral("Window"));
        baGeometry = m_pState->value(QStringLiteral("Geometry")).toByteArray();
        m_pState->endGroup();
    }

    if (baGeometry.isEmpty() || !restoreGeometry(baGeometry)) {
        // Original: entire work area of the monitor
        if (QScreen* pScreen = QGuiApplication::primaryScreen())
            setGeometry(pScreen->availableGeometry());
        else
            resize(1280, 800);
    }
    if (width() < 192 || height() < 160)
        resize(std::max(width(), 192), std::max(height(), 160));
}

void CMainWindow::SaveSettings()
{
    if (!m_pState)
        return;

    m_pState->beginGroup(QStringLiteral("Window"));
    m_pState->setValue(QStringLiteral("Geometry"), saveGeometry());
    m_pState->setValue(QStringLiteral("ScrollX"), m_pView->ScrollPosition().x());
    m_pState->setValue(QStringLiteral("ScrollY"), m_pView->ScrollPosition().y());
    m_pState->endGroup();

    m_pState->beginGroup(QStringLiteral("View"));
    m_pState->setValue(QStringLiteral("GroupMode"), FieldKey(m_view.eGroup));
    m_pState->setValue(QStringLiteral("SortMode"), FieldKey(m_view.eSort));
    m_pState->setValue(QStringLiteral("TextMode"), FieldKey(m_view.eText));
    m_pState->setValue(QStringLiteral("FilterMode"), m_view.filter.Serialize());
    m_pState->setValue(QStringLiteral("ShaderMode"), m_view.shade.Serialize());
    m_pState->endGroup();
    m_pState->sync();
}

void CMainWindow::closeEvent(QCloseEvent* pEvent)
{
    m_pView->HideInfoPopup();
    SaveSettings();
    if (m_pCache)
        m_pCache->SaveIndex();
    QMainWindow::closeEvent(pEvent);
}

// =============================================================================================== Background context menu

void CMainWindow::AddFieldItems(QMenu* pMenu, const QList<QPair<EField, QString>>& aItems, EField eCurrent,
                                const std::function<void(EField)>& fnSelect)
{
    for (const auto& [eField, sText] : aItems) {
        if (sText.isEmpty()) {
            pMenu->addSeparator();
            continue;
        }
        QAction* pAction = pMenu->addAction(sText);
        pAction->setCheckable(true);
        pAction->setChecked(eField == eCurrent);
        const EField eSelected = eField;
        connect(pAction, &QAction::triggered, this, [fnSelect, eSelected]() { fnSelect(eSelected); });
    }
}

void CMainWindow::ShowBackgroundMenu(const QPoint& ptGlobal)
{
    QMenu menu(this);

    // ---- Group by
    QMenu* pGroup = menu.addMenu(tr("Group by"));
    AddFieldItems(pGroup, { { EField::System, tr("Platform") }, { EField::Genre, tr("Genre") }, { EField::Series, tr("Series") },
                            { EField::DevName, tr("Developer") }, { EField::Name, tr("Title") }, { EField::None, {} },
                            { EField::ReleaseDate, tr("Date (Release: Year)") }, { EField::ReleaseDateYM, tr("Date (Release: Year/Month)") } },
                  m_view.eGroup, [this](EField e) { if (e != m_view.eGroup) { m_view.eGroup = e; ApplyView(true); } });

    // ---- Sort by
    QMenu* pSort = menu.addMenu(tr("Sort by"));
    AddFieldItems(pSort, { { EField::ReleaseDate, tr("Date (Release)") }, { EField::None, {} }, { EField::Name, tr("Title") },
                           { EField::System, tr("Platform") }, { EField::Genre, tr("Genre") }, { EField::Series, tr("Series") },
                           { EField::DevName, tr("Developer") } },
                  m_view.eSort, [this](EField e) { if (e != m_view.eSort) { m_view.eSort = e; ApplyView(true); } });

    // ---- Description
    QMenu* pText = menu.addMenu(tr("Description"));
    QAction* pNone = pText->addAction(tr("None"));
    pNone->setCheckable(true);
    pNone->setChecked(m_view.eText == EField::None);
    connect(pNone, &QAction::triggered, this, [this]() { if (m_view.eText != EField::None) { m_view.eText = EField::None; ApplyView(true); } });
    pText->addSeparator();
    AddFieldItems(pText, { { EField::Name, tr("Title") }, { EField::DevName, tr("Developer") }, { EField::System, tr("Platform") },
                           { EField::Genre, tr("Genre") }, { EField::Series, tr("Series") }, { EField::None, {} },
                           { EField::ReleaseDate, tr("Date (Release)") } },
                  m_view.eText, [this](EField e) { if (e != m_view.eText) { m_view.eText = e; ApplyView(true); } });

    menu.addSeparator();

    // ---- Hide
    QMenu* pHide = menu.addMenu(tr("Hide"));
    auto addToggle = [this](QMenu* pMenu, const QString& sText, bool bChecked, const std::function<void()>& fnToggle) {
        QAction* pAction = pMenu->addAction(sText);
        pAction->setCheckable(true);
        pAction->setChecked(bChecked);
        connect(pAction, &QAction::triggered, this, [this, fnToggle]() { fnToggle(); ApplyView(true); });
    };
    SFilter& f = m_view.filter;
    addToggle(pHide, tr("None"), f.IsEmpty(), [&f]() { f = SFilter(); });
    pHide->addSeparator();
    // "Installed"/"Not installed" and "With/without review" are mutually exclusive
    addToggle(pHide, tr("Installed"), f.bHideInstalled, [&f]() { f.bHideInstalled = !f.bHideInstalled; if (f.bHideInstalled) f.bHideUninstalled = false; });
    addToggle(pHide, tr("Uninstalled"), f.bHideUninstalled, [&f]() { f.bHideUninstalled = !f.bHideUninstalled; if (f.bHideUninstalled) f.bHideInstalled = false; });
    pHide->addSeparator();
    addToggle(pHide, tr("Reviewed"), f.bHideReviewed, [&f]() { f.bHideReviewed = !f.bHideReviewed; if (f.bHideReviewed) f.bHideUnreviewed = false; });
    addToggle(pHide, tr("Unreviewed"), f.bHideUnreviewed, [&f]() { f.bHideUnreviewed = !f.bHideUnreviewed; if (f.bHideUnreviewed) f.bHideReviewed = false; });

    auto toggleIn = [](QStringList& a, const QString& s) {
        if (a.contains(s)) a.removeAll(s);
        else               a.append(s);
    };
    if (!m_aGenres.isEmpty() || !m_aSystems.isEmpty())
        pHide->addSeparator();
    if (!m_aGenres.isEmpty()) {
        QMenu* pGenre = pHide->addMenu(tr("All but Genre"));
        for (const QString& sGenre : std::as_const(m_aGenres))
            addToggle(pGenre, sGenre, f.aOnlyGenres.contains(sGenre), [&f, toggleIn, sGenre]() { toggleIn(f.aOnlyGenres, sGenre); });
    }
    if (!m_aSystems.isEmpty()) {
        QMenu* pSystem = pHide->addMenu(tr("All but Platform"));
        for (const QString& sSystem : std::as_const(m_aSystems))
            addToggle(pSystem, sSystem, f.aOnlySystems.contains(sSystem), [&f, toggleIn, sSystem]() { toggleIn(f.aOnlySystems, sSystem); });
    }

    // ---- Shade
    QMenu* pShade = menu.addMenu(tr("Shade"));
    SShade& sh = m_view.shade;
    addToggle(pShade, tr("None"), sh.IsEmpty(), [&sh]() { sh = SShade(); sh.bShadeUninstalled = false; });
    pShade->addSeparator();
    addToggle(pShade, tr("Installed"), sh.bShadeInstalled, [&sh]() { sh.bShadeInstalled = !sh.bShadeInstalled; if (sh.bShadeInstalled) sh.bShadeUninstalled = false; });
    addToggle(pShade, tr("Uninstalled"), sh.bShadeUninstalled, [&sh]() { sh.bShadeUninstalled = !sh.bShadeUninstalled; if (sh.bShadeUninstalled) sh.bShadeInstalled = false; });
    pShade->addSeparator();
    addToggle(pShade, tr("Reviewed"), sh.bShadeReviewed, [&sh]() { sh.bShadeReviewed = !sh.bShadeReviewed; if (sh.bShadeReviewed) sh.bShadeUnreviewed = false; });
    addToggle(pShade, tr("Unreviewed"), sh.bShadeUnreviewed, [&sh]() { sh.bShadeUnreviewed = !sh.bShadeUnreviewed; if (sh.bShadeUnreviewed) sh.bShadeReviewed = false; });

    menu.exec(ptGlobal);
}

// =============================================================================================== Game context menu

void CMainWindow::ShowGameMenu(int nGame, const QPoint& ptGlobal)
{
    const SGame& game = m_aGames.at(nGame);
    const QString sGamePath = game.sGamePath;
    const QStringList aConf = GameLibrary::ConfFiles(sGamePath);
    QMenu menu(this);

    // ---- Play: one entry per *.conf
    QMenu* pPlay = menu.addMenu(tr("Play"));
    for (const QString& sConf : aConf) {
        QAction* pAction = pPlay->addAction(IconForFile(sConf), QFileInfo(sConf).completeBaseName());
        connect(pAction, &QAction::triggered, this, [this, sConf]() { Launch(sConf); });
    }
    pPlay->setEnabled(!aConf.isEmpty());

    // ---- Edit: GameInfo.txt and *.conf
    QMenu* pEdit = menu.addMenu(tr("Edit"));
    const QString sInfo = GameLibrary::ChildCaseInsensitive(sGamePath, QStringLiteral("GameInfo.txt"));
    if (!sInfo.isEmpty()) {
        QAction* pAction = pEdit->addAction(IconForFile(sInfo), QStringLiteral("GameInfo.txt"));
        connect(pAction, &QAction::triggered, this, [this, sInfo]() { EditFile(sInfo); });
    }
    for (const QString& sConf : aConf) {
        QAction* pAction = pEdit->addAction(IconForFile(sConf), QFileInfo(sConf).fileName());
        connect(pAction, &QAction::triggered, this, [this, sConf]() { EditFile(sConf); });
    }
    pEdit->setEnabled(!pEdit->isEmpty());

    // ---- Folders
    QMenu* pFolders = menu.addMenu(tr("Folders"));
    QAction* pFolder = pFolders->addAction(QIcon::fromTheme(QStringLiteral("folder")), QStringLiteral("GameInfo"));
    connect(pFolder, &QAction::triggered, this, [this, nGame]() { OpenFolder(nGame); });

    // ---- Extras / Reviews: files in the subfolders of the same name
    auto addFileMenu = [this, &menu, &sGamePath](const QString& sTitle, const QString& sSubdir) {
        QMenu* pSub = menu.addMenu(sTitle);
        const QString sDir = GameLibrary::ChildCaseInsensitive(sGamePath, sSubdir);
        for (const QString& sFile : GameLibrary::FilesIn(sDir)) {
            QAction* pAction = pSub->addAction(IconForFile(sFile), QFileInfo(sFile).fileName());
            connect(pAction, &QAction::triggered, this, [this, sFile]() { OpenFile(sFile); });
        }
        pSub->setEnabled(!pSub->isEmpty());
    };
    addFileMenu(tr("Extras"), QStringLiteral("Extras"));
    addFileMenu(tr("Reviews"), QStringLiteral("Reviews"));

    // ---- Web search
    if (!game.info.sName.isEmpty()) {
        menu.addSeparator();
        QAction* pGoogle = menu.addAction(QStringLiteral("Google"));
        connect(pGoogle, &QAction::triggered, this, [this, nGame]() { WebSearch(nGame); });
    }

    menu.exec(ptGlobal);
}

// =============================================================================================== Actions

void CMainWindow::Launch(const QString& sConfPath)
{
    if (!QFileInfo(sConfPath).isFile()) {
        QMessageBox::warning(this, windowTitle(), tr("Can't find game config file:\n\t\"%1\"").arg(sConfPath));
        return;
    }
    const SLaunchData launch = GameLibrary::GetLaunchData(sConfPath, m_config);
    if (!launch.bAvailable) {
        QMessageBox::warning(this, windowTitle(), tr("The game cannot be started. Missing:\n\n%1").arg(launch.sMissing));
        return;
    }
    if (!QProcess::startDetached(launch.sProgram, launch.aArgs, launch.sWorkingDir))
        QMessageBox::warning(this, windowTitle(), tr("Could not start \"%1\".").arg(launch.sProgram));
}

void CMainWindow::OpenFile(const QString& sPath)
{
    if (QFileInfo::exists(sPath))
        QDesktopServices::openUrl(QUrl::fromLocalFile(sPath));
}

// TextEditor from the INI: .desktop ID (e.g. "org.kde.kate.desktop"), program name in PATH or
// absolute path. Empty = system default application.
void CMainWindow::EditFile(const QString& sPath)
{
    if (!QFileInfo::exists(sPath))
        return;
    if (m_config.sTextEditor.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(sPath));
    else
        Helpers::openFileListWithHandler(m_config.sTextEditor, { sPath });
}

// Links always open in the system default browser (xdg-settings get default-web-browser)
void CMainWindow::OpenUrl(const QString& sUrl)
{
    QDesktopServices::openUrl(QUrl(sUrl));
}

void CMainWindow::WebSearch(int nGame)
{
    const QString sName = GameLibrary::Unserialize(m_aGames.at(nGame).info.sName);
    if (sName.isEmpty())
        return;
    QString sQuery = QString::fromUtf8(QUrl::toPercentEncoding(sName));
    sQuery.replace(QStringLiteral("%20"), QStringLiteral("+"));
    OpenUrl(QStringLiteral("https://www.google.com/search?q=") + sQuery);
}

void CMainWindow::OpenFolder(int nGame)
{
    // Ctrl+L without a game under the mouse -> GameInfo main folder
    const QString sPath = nGame >= 0 ? m_aGames.at(nGame).sGamePath : m_config.sGameInfoDir;
    if (QFileInfo(sPath).isDir())
        QDesktopServices::openUrl(QUrl::fromLocalFile(sPath));
}

void CMainWindow::ShowHelp()
{
    QMessageBox::information(this, tr("Hotkeys"),
        tr("Ctrl+E\tEdit GameInfo.txt of the game under the mouse\n"
           "Ctrl+G\tSearch on Google\n"
           "Ctrl+L\tOpen the folder of the game under the mouse\n"
           "Ctrl+F\tFilter (Esc closes the filter line)\n"
           "A-Z, 0-9\tJump to group\n"
           "Double-click\tStart game (Launch.conf)\n\n"
           "F5\tReload Game Browser\n"
           "Esc\tClose"));
}

void CMainWindow::OnThumbnailFailed(const QString& sMarker, const QString& sCoverPath)
{
    qWarning("Could not create thumbnail \"%s\" from \"%s\"", qPrintable(sMarker), qPrintable(sCoverPath));
}
