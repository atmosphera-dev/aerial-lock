// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace InterActive {

// Controls implementation detail: publish only IA-owned horizontal geometry.
class DialogButtonBoxLayout : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal actualExtent READ actualExtent WRITE setActualExtent NOTIFY inputsChanged FINAL)
    Q_PROPERTY(qreal nativeWidth READ nativeWidth WRITE setNativeWidth NOTIFY inputsChanged FINAL)
    Q_PROPERTY(qreal availableWidth READ availableWidth WRITE setAvailableWidth NOTIFY inputsChanged FINAL)
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY inputsChanged FINAL)
    Q_PROPERTY(int rowDirection READ rowDirection WRITE setRowDirection NOTIFY inputsChanged FINAL)
    Q_PROPERTY(bool boxMirrored READ boxMirrored WRITE setBoxMirrored NOTIFY inputsChanged FINAL)
    Q_PROPERTY(qreal padLeft READ padLeft WRITE setPadLeft NOTIFY inputsChanged FINAL)
    Q_PROPERTY(qreal padRight READ padRight WRITE setPadRight NOTIFY inputsChanged FINAL)
    Q_PROPERTY(qreal naturalWidth READ naturalWidth NOTIFY changed FINAL)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY changed FINAL)
    Q_PROPERTY(qreal headerWidth READ headerWidth NOTIFY changed FINAL)
    Q_PROPERTY(qreal footerWidth READ footerWidth NOTIFY changed FINAL)
    Q_PROPERTY(qreal committedExtent READ committedExtent NOTIFY changed FINAL)
public:
    explicit DialogButtonBoxLayout(QQuickItem *parent = nullptr) : QQuickItem(parent) {}
    qreal actualExtent() const { return m_pending.actualExtent; }
    qreal nativeWidth() const { return m_pending.nativeWidth; }
    qreal availableWidth() const { return m_pending.availableWidth; }
    int alignment() const { return m_pending.alignment; }
    int rowDirection() const { return m_pending.rowDirection; }
    bool boxMirrored() const { return m_pending.boxMirrored; }
    qreal padLeft() const { return m_pending.padLeft; }
    qreal padRight() const { return m_pending.padRight; }
    void setActualExtent(qreal value);
    void setNativeWidth(qreal value);
    void setAvailableWidth(qreal value);
    void setAlignment(int value);
    void setRowDirection(int value);
    void setBoxMirrored(bool value);
    void setPadLeft(qreal value);
    void setPadRight(qreal value);
    qreal naturalWidth() const { return m_output.naturalWidth; }
    qreal contentWidth() const { return m_output.contentWidth; }
    qreal headerWidth() const { return m_output.headerWidth; }
    qreal footerWidth() const { return m_output.footerWidth; }
    qreal committedExtent() const { return m_committed.actualExtent; }
signals:
    void inputsChanged();
    void changed();
protected:
    void updatePolish() override;
private:
    struct Inputs {
        qreal actualExtent = 0;
        qreal nativeWidth = 0;
        qreal availableWidth = 0;
        int alignment = 0;
        int rowDirection = Qt::LeftToRight;
        bool boxMirrored = false;
        qreal padLeft = 0;
        qreal padRight = 0;
        bool operator==(const Inputs &other) const;
    };
    struct Outputs {
        qreal naturalWidth = 0;
        qreal contentWidth = 0;
        qreal headerWidth = 0;
        qreal footerWidth = 0;
    };
    void queueLayout();
    Inputs m_pending;
    Inputs m_committed;
    Outputs m_output;
    bool m_hasCommit = false;
};

} // namespace InterActive
