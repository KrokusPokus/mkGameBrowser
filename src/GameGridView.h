#ifndef GAMEGRIDVIEW_H
#define GAMEGRIDVIEW_H

#include "GameLibrary.h"
#include "ViewSettings.h"

#include <QAbstractScrollArea>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QTimer>

class CInfoPopup;
class CThumbnailCache;

// Custom-drawn, grouped thumbnail view (corresponds to the window content of Gui 1 in the AHK script).
//
// Deliberately not a QListView: group headers with counter and gradient line, thumbnails with fixed height
// and variable width, multi-line text below them, and games that appear in several groups
// can be implemented 1:1 like the original with a custom layout. Only the visible area
// is painted; there are no widgets per entry.
class CGameGridView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit CGameGridView(QWidget* pParent = nullptr);
    ~CGameGridView() override;

    void SetContent(const QList<SGame>* pGames, const QList<SGroup>& aGroups, EField eText, CThumbnailCache* pCache);

    QPoint ScrollPosition() const;
    void   SetScrollPosition(const QPoint& pt);

    int  GameAt(const QPoint& ptViewport) const;     // game index or -1
    int  GameUnderCursor() const;
    void HideInfoPopup();

signals:
    void LaunchRequested(int nGame);
    void ContextMenuRequested(int nGame, const QPoint& ptGlobal);   // nGame = -1: background
    void EditGameInfoRequested(int nGame);                          // Ctrl+E
    void WebSearchRequested(int nGame);                             // Ctrl+G
    void OpenFolderRequested(int nGame);                            // Ctrl+L (-1: GameInfo folder)
    void ReloadRequested();                                         // F5
    void HelpRequested();                                           // F1

protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* pEvent) override;
    void keyPressEvent(QKeyEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;
    void contextMenuEvent(QContextMenuEvent* pEvent) override;
    bool viewportEvent(QEvent* pEvent) override;
    void changeEvent(QEvent* pEvent) override;

private:
    struct SHeaderItem {
        QString sTitle;
        QString sCount;
        QRect   rcTitle;
        QRect   rcCount;
        int     nCountBaseline = 0;
        QRect   rcLine;
    };
    struct SThumbItem {
        int     nGame = -1;
        QRect   rcImage;
        QRect   rcText;         // empty if there is no text
        QString sText;
    };

    void Relayout();
    void UpdateScrollBars();
    QPoint ContentOffset() const;
    const SThumbItem* ThumbAt(const QPoint& ptViewport) const;
    void ScrollBy(int dx, int dy);
    void ScrollToY(int y);
    void TypeToJump(const QString& sChar);
    void OnHoverTimeout();
    void OnThumbnailReady(const QString& sMarker);

    const QList<SGame>*     m_pGames = nullptr;
    QList<SGroup>           m_aGroups;
    EField                  m_eText = EField::Name;
    QPointer<CThumbnailCache> m_pCache;            // automatically becomes nullptr when the cache is deleted

    QList<SHeaderItem>      m_aHeaders;
    QList<SThumbItem>       m_aThumbs;
    QList<int>              m_aThumbWidths;         // per game (logical pixels), -1 = not yet known
    QSize                   m_szContent;
    int                     m_nLayoutWidth = -1;

    QFont                   m_fontHeader;
    QFont                   m_fontSmall;

    CInfoPopup*             m_pPopup = nullptr;
    QTimer                  m_timerHover;
    QPoint                  m_ptLastMouse;
    int                     m_nPopupThumb = -1;     // Index in m_aThumbs

    QString                 m_sTypeBuffer;
    QTimer                  m_timerTypeClear;
    bool                    m_bIgnoreNextContextMenu = false;
};

#endif // GAMEGRIDVIEW_H
