// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "Presentation.h"
#include <QColor>
#include <memory>

namespace InterActive {

struct StateTableData;

class INTERACTIVE_FOUNDATION_EXPORT StateColors
{
    Q_GADGET
    QML_VALUE_TYPE(stateColors)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor CONSTANT FINAL)
    Q_PROPERTY(QColor foregroundColor READ foregroundColor CONSTANT FINAL)
    Q_PROPERTY(QColor borderColor READ borderColor CONSTANT FINAL)
    Q_PROPERTY(qreal borderWidth READ borderWidth CONSTANT FINAL)
    Q_PROPERTY(QColor focusColor READ focusColor CONSTANT FINAL)
    Q_PROPERTY(QColor focusedBorderColor READ focusedBorderColor CONSTANT FINAL)
    Q_PROPERTY(qreal focusedBorderWidth READ focusedBorderWidth CONSTANT FINAL)
    Q_PROPERTY(QColor fillColor READ fillColor CONSTANT FINAL)
    Q_PROPERTY(QColor trackColor READ trackColor CONSTANT FINAL)
    Q_PROPERTY(QColor thumbColor READ thumbColor CONSTANT FINAL)
    Q_PROPERTY(QColor placeholderTextColor READ placeholderTextColor CONSTANT FINAL)
    Q_PROPERTY(QColor choiceGapColor READ choiceGapColor CONSTANT FINAL)
    Q_PROPERTY(QColor choiceCenterColor READ choiceCenterColor CONSTANT FINAL)
    Q_PROPERTY(QColor choiceMarkColor READ choiceMarkColor CONSTANT FINAL)
    Q_PROPERTY(QColor choiceTrackColor READ choiceTrackColor CONSTANT FINAL)
public:
    StateColors() = default;
    StateColors(QColor background, QColor foreground, QColor border, QColor focus,
                QColor fill = {}, QColor track = {}, QColor placeholder = {}, QColor focusedBorder = {},
                QColor choiceGap = {}, QColor choiceCenter = {}, QColor choiceMark = {}, QColor choiceTrack = {},
                QColor thumb = {}, bool conditionalBorder = false)
        : m_background(std::move(background)), m_foreground(std::move(foreground)),
          m_border(std::move(border)), m_focus(std::move(focus)), m_fill(std::move(fill)),
          m_track(std::move(track)), m_placeholder(std::move(placeholder)),
          m_focusedBorder(std::move(focusedBorder)), m_choiceGap(std::move(choiceGap)),
          m_choiceCenter(std::move(choiceCenter)), m_choiceMark(std::move(choiceMark)), m_choiceTrack(std::move(choiceTrack)),
          m_thumb(std::move(thumb)), m_conditionalBorder(conditionalBorder) {}
    QColor backgroundColor() const { return m_background; }
    QColor foregroundColor() const { return m_foreground; }
    QColor borderColor() const { return m_border; }
    qreal borderWidth() const { return m_borderWidth; }
    QColor focusColor() const { return m_focus; }
    QColor focusedBorderColor() const { return m_focusedBorder; }
    qreal focusedBorderWidth() const { return m_focusedBorderWidth; }
    QColor fillColor() const { return m_fill; }
    QColor trackColor() const { return m_track; }
    QColor thumbColor() const { return m_thumb; }
    QColor placeholderTextColor() const { return m_placeholder; }
    QColor choiceGapColor() const { return m_choiceGap; }
    QColor choiceCenterColor() const { return m_choiceCenter; }
    QColor choiceMarkColor() const { return m_choiceMark; }
    QColor choiceTrackColor() const { return m_choiceTrack; }
    bool operator==(const StateColors &other) const
    {
        return m_background == other.m_background && m_foreground == other.m_foreground
            && m_border == other.m_border && m_focus == other.m_focus
            && m_conditionalBorder == other.m_conditionalBorder && m_borderWidth == other.m_borderWidth
            && m_fill == other.m_fill && m_track == other.m_track && m_placeholder == other.m_placeholder
            && m_focusedBorder == other.m_focusedBorder && m_focusedBorderWidth == other.m_focusedBorderWidth
            && m_choiceGap == other.m_choiceGap && m_choiceCenter == other.m_choiceCenter
            && m_choiceMark == other.m_choiceMark && m_choiceTrack == other.m_choiceTrack && m_thumb == other.m_thumb;
    }
private:
    QColor m_background, m_foreground, m_border, m_focus, m_fill, m_track, m_placeholder;
    QColor m_focusedBorder;
    QColor m_choiceGap, m_choiceCenter, m_choiceMark, m_choiceTrack;
    QColor m_thumb;
    qreal m_focusedBorderWidth = 0;
    bool m_conditionalBorder = false;
    qreal m_borderWidth = 0;
    friend class StateTable;
};

// A cheap owning value, not another QObject per control. Reading the facade's
// stateTable property establishes the binding dependency before the array lookup.
class INTERACTIVE_FOUNDATION_EXPORT StateTable
{
    Q_GADGET
    QML_VALUE_TYPE(stateTable)
    Q_PROPERTY(QColor selectionColor READ selectionColor CONSTANT FINAL)
    Q_PROPERTY(QColor selectedTextColor READ selectedTextColor CONSTANT FINAL)
    Q_PROPERTY(QColor linkColor READ linkColor CONSTANT FINAL)
public:
    StateTable() = default;
    explicit StateTable(std::shared_ptr<const StateTableData> data, qreal focusWidth = 2, qreal borderWidth = 1)
        : m_data(std::move(data)), m_focusWidth(focusWidth), m_borderWidth(borderWidth) {}
    Q_INVOKABLE InterActive::StateColors colors(InterActive::Presentation::State state) const;
    QColor selectionColor() const;
    QColor selectedTextColor() const;
    QColor linkColor() const;
    bool operator==(const StateTable &other) const;
    bool operator!=(const StateTable &other) const { return !(*this == other); }
private:
    std::shared_ptr<const StateTableData> m_data;
    // Geometry is a snapshot alongside shared colors, never a palette cache key.
    qreal m_focusWidth = 0;
    qreal m_borderWidth = 0;
};

} // namespace InterActive
