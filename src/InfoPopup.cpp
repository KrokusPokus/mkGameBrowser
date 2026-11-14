#include "InfoPopup.h"

#include <QGridLayout>
#include <QLabel>

namespace {
constexpr int kMargin        = 20;
constexpr int kLabelColumn   = 87;      // LeftMargin3 - LeftMargin1
constexpr int kCommentsWidth = 418;
constexpr int kGap           = 8;
const QString kStyle = QStringLiteral(
    "CInfoPopup { background-color: #222225; border: 1px solid #4a4a50; }"
    "QLabel { color: #DDDDDD; background: transparent; }"
    "QLabel#comments-heading { color: #FF0000; font-weight: bold; }");
} // namespace

CInfoPopup::CInfoPopup(QWidget* pParent)
    : QFrame(pParent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setStyleSheet(kStyle);
    m_pLayout = new QGridLayout(this);
    m_pLayout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    m_pLayout->setHorizontalSpacing(0);
    m_pLayout->setVerticalSpacing(6);
    m_pLayout->setColumnMinimumWidth(0, kLabelColumn);
}

void CInfoPopup::Clear()
{
    while (QLayoutItem* pItem = m_pLayout->takeAt(0)) {
        delete pItem->widget();
        delete pItem;
    }
}

void CInfoPopup::SetInfo(const SGameInfo& info)
{
    Clear();
    int nRow = 0;
    auto addRow = [this, &nRow](const QString& sLabel, const QString& sValue) {
        if (sValue.isEmpty())
            return;
        m_pLayout->addWidget(new QLabel(sLabel, this), nRow, 0, Qt::AlignTop);
        auto* pValue = new QLabel(sValue, this);
        pValue->setTextFormat(Qt::PlainText);
        m_pLayout->addWidget(pValue, nRow, 1, Qt::AlignTop);
        ++nRow;
    };

    addRow(tr("Title"), GameLibrary::Unserialize(info.sName));
    addRow(tr("Developer"), info.sDevName);
    addRow(tr("System"), info.sSystem);
    addRow(tr("Released"), info.sReleaseDate.compare(QLatin1String("tba"), Qt::CaseInsensitive) == 0
                               ? QStringLiteral("TBA") : info.sReleaseDate);

    if (!info.sComments.isEmpty()) {
        auto* pHeading = new QLabel(tr("Private Comments:"), this);
        pHeading->setObjectName(QStringLiteral("comments-heading"));
        QFont fontHeading = pHeading->font();
        fontHeading.setPointSizeF(fontHeading.pointSizeF() + 2.0);
        pHeading->setFont(fontHeading);
        if (nRow > 0)
            m_pLayout->setRowMinimumHeight(nRow++, 5);
        m_pLayout->addWidget(pHeading, nRow++, 0, 1, 2);

        auto* pComments = new QLabel(GameLibrary::Unserialize(info.sComments), this);
        pComments->setTextFormat(Qt::PlainText);
        pComments->setWordWrap(true);
        pComments->setFixedWidth(kCommentsWidth);
        pComments->setContentsMargins(5, 0, 0, 0);
        m_pLayout->addWidget(pComments, nRow++, 0, 1, 2);
    }
    adjustSize();
}

void CInfoPopup::ShowNear(const QRect& rcThumb, const QPoint& ptInThumb, int nBelowThumb, const QRect& rcWork)
{
    adjustSize();
    const QSize sz = size();
    const QRect rcArea = rcWork.isValid() ? rcWork : QRect(rcThumb.center() - QPoint(5000, 5000), QSize(10000, 10000));

    // Horizontally centered over the thumbnail
    int x = rcThumb.center().x() - sz.width() / 2;
    x = std::clamp(x, rcArea.left(), std::max(rcArea.left(), rcArea.right() + 1 - sz.width()));
    int y = 0;

    const int nBelowY = rcThumb.bottom() + 1 + nBelowThumb;
    const int nAboveY = rcThumb.top() - kGap - sz.height();
    const bool bFitsBelow = nBelowY + sz.height() <= rcArea.bottom() + 1;
    const bool bFitsAbove = nAboveY >= rcArea.top();

    auto placeBeside = [&]() {
        y = rcThumb.center().y() - sz.height() / 2;
        y = std::clamp(y, rcArea.top(), std::max(rcArea.top(), rcArea.bottom() + 1 - sz.height()));
        const int nRightX = rcThumb.right() + 1 + kGap;
        const int nLeftX = rcThumb.left() - kGap - sz.width();
        if (ptInThumb.x() > rcThumb.width() / 2)    // right half -> prefer to the right
            x = (nRightX + sz.width() <= rcArea.right() + 1) ? nRightX : nLeftX;
        else
            x = (nLeftX >= rcArea.left()) ? nLeftX : nRightX;
    };

    if (ptInThumb.y() > rcThumb.height() / 2) {     // lower half -> prefer below
        if (bFitsBelow)      y = nBelowY;
        else if (bFitsAbove) y = nAboveY;
        else                 placeBeside();
    }
    else {                                          // upper half -> prefer above
        if (bFitsAbove)      y = nAboveY;
        else if (bFitsBelow) y = nBelowY;
        else                 placeBeside();
    }

    move(x, y);
    show();
    raise();
}
