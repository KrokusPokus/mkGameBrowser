#include "GameGridView.h"

#include "InfoPopup.h"
#include "ThumbnailCache.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QWheelEvent>

#include <climits>

namespace {

// Dimensions from the AHK script (there * A_ScreenDPI/96, here logical pixels)
constexpr int kThumbHeight      = 172;
constexpr int kLeftMarginImage  = 32;
constexpr int kHeaderToImage    = 32;
constexpr int kImageDistanceH   = 32;
constexpr int kImageDistanceV   = 32;
constexpr int kGroupDistanceV   = 40;
constexpr int kGroupDistanceTop = 18;   // less spacing at the top or after groups without images
constexpr int kUnderlineDist    = 8;
constexpr int kHeaderX          = 14;
constexpr int kCountGapX        = 10;
constexpr int kTextGapY         = 7;
constexpr int kRightReserve     = 20;   // (g_iClientW - 20)
constexpr int kLineWidth        = 300;
constexpr int kExtraScroll      = 16;   // "nMax + 16 so we can scroll a little bit further out"
constexpr int kLineStep         = 42;   // SB_LINEUP/SB_LINEDOWN
constexpr int kHoverDelayMs     = 400;  // TrackMouseEvent dwHoverTime
constexpr int kTypeResetMs      = 500;

const QColor kBackground(0x20, 0x20, 0x20);
const QColor kPlaceholder(0x32, 0x32, 0x32);
const QColor kTextColor(0xFF, 0xFF, 0xFF);
const QColor kCountColor(0x88, 0x88, 0x88);
const QColor kLineFrom(0xCB, 0xCB, 0xCB);   // IMG\Gradient.bmp: 300x1, light -> background
const QColor kLineTo(0x21, 0x21, 0x21);

constexpr int kTextFlags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;

} // namespace

CGameGridView::CGameGridView(QWidget* pParent)
    : QAbstractScrollArea(pParent)
{
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);       // Original: WS_VSCROLL always present
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);    // paintEvent() draws the background itself
    QPalette pal = palette();
    pal.setColor(QPalette::Window, kBackground);
    pal.setColor(QPalette::Base, kBackground);
    setPalette(pal);

    m_fontSmall = font();
    m_fontHeader = font();
    m_fontHeader.setPointSizeF(14.0);
    m_fontHeader.setWeight(QFont::Normal);

    m_pPopup = new CInfoPopup(this);

    m_timerHover.setSingleShot(true);
    m_timerHover.setInterval(kHoverDelayMs);
    connect(&m_timerHover, &QTimer::timeout, this, &CGameGridView::OnHoverTimeout);

    m_timerTypeClear.setSingleShot(true);
    m_timerTypeClear.setInterval(kTypeResetMs);
    connect(&m_timerTypeClear, &QTimer::timeout, this, [this]() { m_sTypeBuffer.clear(); });
}

CGameGridView::~CGameGridView() = default;

void CGameGridView::SetContent(const QList<SGame>* pGames, const QList<SGroup>& aGroups, EField eText, CThumbnailCache* pCache)
{
    HideInfoPopup();
    if (m_pCache != pCache) {
        if (m_pCache)
            disconnect(m_pCache, nullptr, this, nullptr);
        m_pCache = pCache;
        if (m_pCache)
            connect(m_pCache, &CThumbnailCache::ThumbnailReady, this, &CGameGridView::OnThumbnailReady);
    }
    m_pGames = pGames;
    m_aGroups = aGroups;
    m_eText = eText;
    m_aThumbWidths = QList<int>(m_pGames ? m_pGames->size() : 0, -1);
    m_nLayoutWidth = -1;
    Relayout();
    viewport()->update();
}

// =============================================================================================== Layout

void CGameGridView::Relayout()
{
    m_aHeaders.clear();
    m_aThumbs.clear();
    m_nPopupThumb = -1;
    if (!m_pGames || !m_pCache) {
        m_szContent = {};
        UpdateScrollBars();
        return;
    }

    const int nClientW = viewport()->width();
    m_nLayoutWidth = nClientW;
    const QFontMetrics fmHeader(m_fontHeader);
    const QFontMetrics fmSmall(m_fontSmall);

    int  nYMax = 0;
    int  nXMax = 0;
    bool bAfterImage = false;

    for (const SGroup& group : std::as_const(m_aGroups)) {
        const int nMembers = group.aMembers.size();
        if (nMembers == 0 || nMembers == group.nHidden)
            continue;   // group without visible games -> no header

        // ---- Header "Title  (n total / m hidden)" + gradient line
        SHeaderItem header;
        header.sTitle = group.sTitle;
        header.sCount = group.nHidden > 0 ? tr("(%1 total / %2 hidden)").arg(nMembers).arg(group.nHidden)
                                          : tr("(%1 total)").arg(nMembers);
        int nY = nYMax + kGroupDistanceV;
        if (!bAfterImage)
            nY -= kGroupDistanceTop;

        header.rcTitle = QRect(kHeaderX, nY, fmHeader.horizontalAdvance(header.sTitle), fmHeader.height());
        const int nBaseline = nY + fmHeader.ascent();
        header.rcCount = QRect(header.rcTitle.right() + 1 + kCountGapX, nBaseline - fmSmall.ascent(),
                               fmSmall.horizontalAdvance(header.sCount), fmSmall.height());
        header.nCountBaseline = nBaseline;
        const int nLineY = std::max(header.rcTitle.bottom(), header.rcCount.bottom()) + 1 + kUnderlineDist;
        header.rcLine = QRect(0, nLineY, kLineWidth, 1);
        m_aHeaders.append(header);

        nYMax = header.rcLine.bottom() + 1;
        nXMax = std::max({ nXMax, header.rcLine.right() + 1, header.rcCount.right() + 1 });
        bool bAfterHeader = true;
        bAfterImage = false;

        // ---- Thumbnails (wrap as soon as the next image would extend past the right edge)
        int nPreviousX = 0;
        QPoint ptNext;
        for (const int nGame : group.aMembers) {
            const SGame& game = m_pGames->at(nGame);
            if (!game.bVisible)
                continue;

            int& nWidth = m_aThumbWidths[nGame];
            if (nWidth < 0)
                nWidth = m_pCache->Prepare(game.sThumbMarker, game.sCoverPath);

            QPoint ptImage;
            if (nPreviousX + kImageDistanceH + nWidth > nClientW - kRightReserve) {
                ptImage = QPoint(kLeftMarginImage, nYMax + kImageDistanceV);
                nPreviousX = kLeftMarginImage + nWidth;
            }
            else if (bAfterHeader) {
                ptImage = QPoint(kLeftMarginImage, nYMax + kHeaderToImage);
                nPreviousX = kLeftMarginImage + nWidth;
            }
            else {
                ptImage = ptNext;
                nPreviousX += kImageDistanceH + nWidth;
            }

            SThumbItem item;
            item.nGame = nGame;
            item.rcImage = QRect(ptImage, QSize(nWidth, kThumbHeight));

            if (m_eText != EField::None) {
                QString sText = game.Field(m_eText);
                if (m_eText == EField::Name)
                    sText = GameLibrary::Unserialize(sText);
                if (!sText.isEmpty()) {
                    item.sText = sText;
                    const QRect rcBound = fmSmall.boundingRect(QRect(0, 0, nWidth, INT_MAX / 2), kTextFlags, sText);
                    item.rcText = QRect(ptImage.x(), item.rcImage.bottom() + 1 + kTextGapY, nWidth, rcBound.height());
                }
            }

            const int nBottom = item.rcText.isNull() ? item.rcImage.bottom() + 1 : item.rcText.bottom() + 1;
            nYMax = std::max(nYMax, nBottom);
            nXMax = std::max(nXMax, item.rcImage.right() + 1);
            ptNext = QPoint(item.rcImage.right() + 1 + kImageDistanceH, item.rcImage.top());
            m_aThumbs.append(item);

            bAfterHeader = false;
            bAfterImage = true;
        }
    }

    m_szContent = QSize(nXMax + kExtraScroll, nYMax + kExtraScroll);
    UpdateScrollBars();
}

void CGameGridView::UpdateScrollBars()
{
    const QSize szView = viewport()->size();
    horizontalScrollBar()->setRange(0, std::max(0, m_szContent.width() - szView.width()));
    horizontalScrollBar()->setPageStep(szView.width());
    horizontalScrollBar()->setSingleStep(kLineStep);
    verticalScrollBar()->setRange(0, std::max(0, m_szContent.height() - szView.height()));
    verticalScrollBar()->setPageStep(szView.height());
    verticalScrollBar()->setSingleStep(kLineStep);
}

QPoint CGameGridView::ContentOffset() const
{
    return { horizontalScrollBar()->value(), verticalScrollBar()->value() };
}

QPoint CGameGridView::ScrollPosition() const
{
    return ContentOffset();
}

void CGameGridView::SetScrollPosition(const QPoint& pt)
{
    horizontalScrollBar()->setValue(pt.x());
    verticalScrollBar()->setValue(pt.y());
}

// =============================================================================================== Painting

void CGameGridView::paintEvent(QPaintEvent* pEvent)
{
    QPainter painter(viewport());
    painter.fillRect(pEvent->rect(), kBackground);      // Gui,Color,202020
    const QPoint ptOffset = ContentOffset();
    const QRect rcVisible = pEvent->rect().translated(ptOffset);
    painter.translate(-ptOffset);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // ---- Headers
    for (const SHeaderItem& header : std::as_const(m_aHeaders)) {
        const QRect rcAll = header.rcTitle.united(header.rcCount).united(header.rcLine);
        if (!rcAll.intersects(rcVisible))
            continue;
        painter.setFont(m_fontHeader);
        painter.setPen(kTextColor);
        painter.drawText(header.rcTitle, Qt::AlignLeft | Qt::AlignTop, header.sTitle);

        painter.setFont(m_fontSmall);
        painter.setPen(kCountColor);
        painter.drawText(QPoint(header.rcCount.left(), header.nCountBaseline), header.sCount);

        QLinearGradient gradient(header.rcLine.topLeft(), header.rcLine.topRight());
        gradient.setColorAt(0.0, kLineFrom);
        gradient.setColorAt(1.0, kLineTo);
        painter.fillRect(header.rcLine, gradient);
    }

    // ---- Thumbnails
    painter.setFont(m_fontSmall);
    for (const SThumbItem& item : std::as_const(m_aThumbs)) {
        const QRect rcAll = item.rcText.isNull() ? item.rcImage : item.rcImage.united(item.rcText);
        if (!rcAll.intersects(rcVisible))
            continue;
        const SGame& game = m_pGames->at(item.nGame);
        const QPixmap pm = m_pCache->Pixmap(game.sThumbMarker, game.bShaded);
        if (pm.isNull())
            painter.fillRect(item.rcImage, kPlaceholder);     // still being generated
        else
            painter.drawPixmap(item.rcImage, pm);

        if (!item.sText.isEmpty()) {
            painter.setPen(kTextColor);
            painter.drawText(item.rcText, kTextFlags, item.sText);
        }
    }
}

void CGameGridView::OnThumbnailReady(const QString& sMarker)
{
    const QPoint ptOffset = ContentOffset();
    const QRect rcView(ptOffset, viewport()->size());
    for (const SThumbItem& item : std::as_const(m_aThumbs)) {
        if (m_pGames->at(item.nGame).sThumbMarker == sMarker && item.rcImage.intersects(rcView))
            viewport()->update(item.rcImage.translated(-ptOffset));
    }
}

void CGameGridView::resizeEvent(QResizeEvent* pEvent)
{
    QAbstractScrollArea::resizeEvent(pEvent);
    HideInfoPopup();
    if (viewport()->width() != m_nLayoutWidth)
        Relayout();     // adapt line wrapping to the new window width (original: only after reloading)
    else
        UpdateScrollBars();
}

void CGameGridView::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    HideInfoPopup();
    viewport()->update();
}

// =============================================================================================== Hit testing

const CGameGridView::SThumbItem* CGameGridView::ThumbAt(const QPoint& ptViewport) const
{
    const QPoint pt = ptViewport + ContentOffset();
    for (const SThumbItem& item : m_aThumbs) {
        if (item.rcImage.contains(pt))
            return &item;
    }
    return nullptr;
}

int CGameGridView::GameAt(const QPoint& ptViewport) const
{
    const SThumbItem* pItem = ThumbAt(ptViewport);
    return pItem ? pItem->nGame : -1;
}

int CGameGridView::GameUnderCursor() const
{
    const QPoint pt = viewport()->mapFromGlobal(QCursor::pos());
    return viewport()->rect().contains(pt) ? GameAt(pt) : -1;
}

// =============================================================================================== Scrolling

void CGameGridView::ScrollBy(int dx, int dy)
{
    if (dx) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    if (dy) verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void CGameGridView::ScrollToY(int y)
{
    verticalScrollBar()->setValue(y);
}

void CGameGridView::wheelEvent(QWheelEvent* pEvent)
{
    HideInfoPopup();
    const Qt::KeyboardModifiers mods = pEvent->modifiers();
    const bool bRightButton = pEvent->buttons() & Qt::RightButton;
    const bool bHorizontal = (mods & Qt::ShiftModifier) || (pEvent->angleDelta().x() != 0 && pEvent->angleDelta().y() == 0);
    const int nDelta = bHorizontal && pEvent->angleDelta().x() != 0 ? pEvent->angleDelta().x() : pEvent->angleDelta().y();
    if (nDelta == 0)
        return;

    QScrollBar* pBar = bHorizontal ? horizontalScrollBar() : verticalScrollBar();
    int nStep = 0;
    if ((mods & Qt::ControlModifier) || bRightButton) {
        // Page-wise; with the right mouse button held, no context menu afterwards (as in the original)
        nStep = (nDelta > 0 ? -1 : 1) * pBar->pageStep();
        m_bIgnoreNextContextMenu = bRightButton;
    }
    else {
        nStep = -nDelta * kLineStep / 120;
        if (nStep == 0)
            nStep = nDelta > 0 ? -1 : 1;
        m_bIgnoreNextContextMenu = false;
    }
    pBar->setValue(pBar->value() + nStep);
    pEvent->accept();
}

void CGameGridView::keyPressEvent(QKeyEvent* pEvent)
{
    HideInfoPopup();
    const Qt::KeyboardModifiers mods = pEvent->modifiers();
    const bool bShift = mods & Qt::ShiftModifier;
    const int nKey = pEvent->key();

    if (mods & Qt::ControlModifier) {
        switch (nKey) {
            case Qt::Key_E: { const int n = GameUnderCursor(); if (n >= 0) emit EditGameInfoRequested(n); return; }
            case Qt::Key_G: { const int n = GameUnderCursor(); if (n >= 0) emit WebSearchRequested(n); return; }
            case Qt::Key_L: emit OpenFolderRequested(GameUnderCursor()); return;
            default: break;
        }
        QAbstractScrollArea::keyPressEvent(pEvent);
        return;
    }

    QScrollBar* pBar = bShift ? horizontalScrollBar() : verticalScrollBar();
    switch (nKey) {
        case Qt::Key_PageUp:   pBar->setValue(pBar->value() - pBar->pageStep()); return;
        case Qt::Key_PageDown: pBar->setValue(pBar->value() + pBar->pageStep()); return;
        case Qt::Key_Home:     pBar->setValue(pBar->minimum()); return;
        case Qt::Key_End:      pBar->setValue(pBar->maximum()); return;
        case Qt::Key_Left:     ScrollBy(-kLineStep, 0); return;
        case Qt::Key_Right:    ScrollBy(kLineStep, 0); return;
        case Qt::Key_Up:       ScrollBy(0, -kLineStep); return;
        case Qt::Key_Down:     ScrollBy(0, kLineStep); return;
        case Qt::Key_F1:       emit HelpRequested(); return;
        case Qt::Key_F5:       emit ReloadRequested(); return;
        default: break;
    }

    // Letters/digits: jump to the first header that starts with the input
    const QString sText = pEvent->text();
    if (sText.size() == 1 && sText.at(0).isLetterOrNumber()) {
        TypeToJump(sText);
        return;
    }
    QAbstractScrollArea::keyPressEvent(pEvent);
}

void CGameGridView::TypeToJump(const QString& sChar)
{
    m_timerTypeClear.start();
    m_sTypeBuffer += sChar;
    for (const SHeaderItem& header : std::as_const(m_aHeaders)) {
        if (header.sTitle.startsWith(m_sTypeBuffer, Qt::CaseInsensitive)) {
            ScrollToY(header.rcTitle.top() - 3);
            return;
        }
    }
}

// =============================================================================================== Mouse

void CGameGridView::mouseDoubleClickEvent(QMouseEvent* pEvent)
{
    if (pEvent->button() == Qt::LeftButton) {
        HideInfoPopup();
        const int nGame = GameAt(pEvent->position().toPoint());
        if (nGame >= 0)
            emit LaunchRequested(nGame);
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(pEvent);
}

void CGameGridView::mousePressEvent(QMouseEvent* pEvent)
{
    HideInfoPopup();
    if (pEvent->button() == Qt::RightButton)
        m_bIgnoreNextContextMenu = false;
    QAbstractScrollArea::mousePressEvent(pEvent);
}

void CGameGridView::mouseMoveEvent(QMouseEvent* pEvent)
{
    const QPoint pt = pEvent->position().toPoint();
    if (pt == m_ptLastMouse)
        return;
    m_ptLastMouse = pt;

    // Different item under the mouse -> close popup immediately (as in the original)
    if (m_pPopup->isVisible()) {
        const SThumbItem* pItem = ThumbAt(pt);
        const int nThumb = pItem ? static_cast<int>(pItem - m_aThumbs.constData()) : -1;
        if (nThumb != m_nPopupThumb)
            HideInfoPopup();
    }
    m_timerHover.start();
}

void CGameGridView::contextMenuEvent(QContextMenuEvent* pEvent)
{
    HideInfoPopup();
    if (m_bIgnoreNextContextMenu) {
        m_bIgnoreNextContextMenu = false;
        return;
    }
    const QPoint ptViewport = viewport()->mapFromGlobal(pEvent->globalPos());
    emit ContextMenuRequested(GameAt(ptViewport), pEvent->globalPos());
}

bool CGameGridView::viewportEvent(QEvent* pEvent)
{
    if (pEvent->type() == QEvent::Leave) {
        m_timerHover.stop();
        HideInfoPopup();
    }
    return QAbstractScrollArea::viewportEvent(pEvent);
}

void CGameGridView::changeEvent(QEvent* pEvent)
{
    if (pEvent->type() == QEvent::ActivationChange)
        HideInfoPopup();
    QAbstractScrollArea::changeEvent(pEvent);
}

// =============================================================================================== Info-Popup

void CGameGridView::HideInfoPopup()
{
    m_timerHover.stop();
    if (m_pPopup && m_pPopup->isVisible())
        m_pPopup->hide();
    m_nPopupThumb = -1;
}

void CGameGridView::OnHoverTimeout()
{
    // Not while modifiers or the right mouse button are pressed
    const Qt::KeyboardModifiers mods = QApplication::queryKeyboardModifiers();
    if ((mods & (Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier)) || (QApplication::mouseButtons() & Qt::RightButton))
        return;
    if (!isActiveWindow())
        return;

    const QPoint ptViewport = viewport()->mapFromGlobal(QCursor::pos());
    const SThumbItem* pItem = viewport()->rect().contains(ptViewport) ? ThumbAt(ptViewport) : nullptr;
    if (!pItem) {
        HideInfoPopup();
        return;
    }
    const int nThumb = static_cast<int>(pItem - m_aThumbs.constData());
    if (nThumb == m_nPopupThumb && m_pPopup->isVisible())
        return;

    // Read values fresh from the file (ControlText2Info(...,FromFile=1))
    const SGame& game = m_pGames->at(pItem->nGame);
    m_pPopup->SetInfo(GameLibrary::ReadGameInfo(game.sGamePath));

    const QPoint ptOffset = ContentOffset();
    const QRect rcThumbGlobal(viewport()->mapToGlobal(pItem->rcImage.topLeft() - ptOffset), pItem->rcImage.size());
    const QPoint ptInThumb = ptViewport + ptOffset - pItem->rcImage.topLeft();
    const int nBelowThumb = pItem->rcText.isNull() ? 8 : 8 + kTextGapY + pItem->rcText.height();
    QScreen* pScreen = screen();
    m_pPopup->ShowNear(rcThumbGlobal, ptInThumb, nBelowThumb, pScreen ? pScreen->availableGeometry() : QRect());
    m_nPopupThumb = nThumb;
}
