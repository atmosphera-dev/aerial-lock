// SPDX-License-Identifier: LGPL-3.0-or-later
#include "DialogButtonBoxLayout.h"
#include <algorithm>

namespace InterActive {

bool DialogButtonBoxLayout::Inputs::operator==(const Inputs &other) const
{
    return actualExtent == other.actualExtent && nativeWidth == other.nativeWidth
        && availableWidth == other.availableWidth && alignment == other.alignment
        && rowDirection == other.rowDirection && boxMirrored == other.boxMirrored
        && padLeft == other.padLeft && padRight == other.padRight;
}

void DialogButtonBoxLayout::queueLayout()
{
    polish();
    emit inputsChanged();
}

void DialogButtonBoxLayout::setActualExtent(qreal value)
{
    if (m_pending.actualExtent == value)
        return;
    m_pending.actualExtent = value;
    queueLayout();
}

void DialogButtonBoxLayout::setNativeWidth(qreal value)
{
    if (m_pending.nativeWidth == value)
        return;
    m_pending.nativeWidth = value;
    queueLayout();
}

void DialogButtonBoxLayout::setAvailableWidth(qreal value)
{
    if (m_pending.availableWidth == value)
        return;
    m_pending.availableWidth = value;
    queueLayout();
}

void DialogButtonBoxLayout::setAlignment(int value)
{
    if (m_pending.alignment == value)
        return;
    m_pending.alignment = value;
    queueLayout();
}

void DialogButtonBoxLayout::setRowDirection(int value)
{
    if (m_pending.rowDirection == value)
        return;
    m_pending.rowDirection = value;
    queueLayout();
}

void DialogButtonBoxLayout::setBoxMirrored(bool value)
{
    if (m_pending.boxMirrored == value)
        return;
    m_pending.boxMirrored = value;
    queueLayout();
}

void DialogButtonBoxLayout::setPadLeft(qreal value)
{
    if (m_pending.padLeft == value)
        return;
    m_pending.padLeft = value;
    queueLayout();
}

void DialogButtonBoxLayout::setPadRight(qreal value)
{
    if (m_pending.padRight == value)
        return;
    m_pending.padRight = value;
    queueLayout();
}

void DialogButtonBoxLayout::updatePolish()
{
    const Inputs next = m_pending;
    if (m_hasCommit && next == m_committed)
        return;

    Outputs out;
    out.naturalWidth = ((next.alignment & Qt::AlignHorizontal_Mask)
        ? next.actualExtent : next.nativeWidth) + next.padLeft + next.padRight;
    out.contentWidth = std::max(next.actualExtent, next.availableWidth);
    const qreal viewWidth = next.alignment == 0 ? next.availableWidth : out.contentWidth;
    int flags = next.alignment;
    if (!(flags & Qt::AlignHorizontal_Mask))
        flags |= Qt::AlignLeft;
    if (!(flags & Qt::AlignAbsolute) && (flags & (Qt::AlignLeft | Qt::AlignRight))) {
        if (next.boxMirrored)
            flags ^= Qt::AlignLeft | Qt::AlignRight;
        flags |= Qt::AlignAbsolute;
    }
    const qreal slack = std::max(qreal(0), viewWidth - next.actualExtent);
    qreal left = 0;
    qreal right = 0;
    if (next.alignment != 0) {
        if (flags & Qt::AlignRight)
            left = slack;
        else if (flags & Qt::AlignHCenter)
            left = slack / 2;
        right = slack - left;
    }
    const bool rtl = next.rowDirection == Qt::RightToLeft;
    out.headerWidth = rtl ? right : left;
    out.footerWidth = rtl ? left : right;

    // Bindings see a whole committed record, never partially mutated inputs.
    m_output = out;
    m_committed = next;
    m_hasCommit = true;
    emit changed();
}

} // namespace InterActive
