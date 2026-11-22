// SPDX-License-Identifier: LGPL-3.0-or-later
#include "DocumentProbe.h"
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QTextBlock>
#include <QQmlEngine>

InterActive::Appearance *DocumentProbe::createAppearance()
{
    auto *appearance = new InterActive::Appearance(this);
    QQmlEngine::setObjectOwnership(appearance, QQmlEngine::CppOwnership);
    return appearance;
}

bool DocumentProbe::destroyAppearance(InterActive::Appearance *appearance)
{
    if (!appearance || appearance->parent() != this)
        return false;
    delete appearance;
    return true;
}

QVariant DocumentProbe::countValue(const QVariant &value, const QString &label)
{
    ++m_counts[label];
    return value;
}

QVariantMap DocumentProbe::counts() const
{
    QVariantMap result;
    for (auto it = m_counts.cbegin(); it != m_counts.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

void DocumentProbe::setDocument(QQuickTextDocument *document)
{
    if (m_wrapper == document)
        return;
    disconnect(m_wrapperConnection);
    m_wrapper = document;
    if (document)
        m_wrapperConnection = connect(document, &QQuickTextDocument::textDocumentChanged,
            this, &DocumentProbe::observeDocument);
    observeDocument();
    emit documentChanged();
}

void DocumentProbe::observeDocument()
{
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_document = m_wrapper ? m_wrapper->textDocument() : nullptr;
    if (!m_document)
        return;
    m_connections.append(connect(m_document, &QTextDocument::contentsChange, this, [this] { ++m_counts[QStringLiteral("contentsChange")]; }));
    m_connections.append(connect(m_document, &QTextDocument::contentsChanged, this, [this] { ++m_counts[QStringLiteral("contentsChanged")]; }));
    m_connections.append(connect(m_document, &QTextDocument::undoAvailable, this, [this] { ++m_counts[QStringLiteral("undoAvailable")]; }));
    m_connections.append(connect(m_document, &QTextDocument::redoAvailable, this, [this] { ++m_counts[QStringLiteral("redoAvailable")]; }));
    auto *layout = m_document->documentLayout();
    m_connections.append(connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged,
        this, [this] { ++m_counts[QStringLiteral("documentSizeChanged")]; }));
    m_connections.append(connect(layout, &QAbstractTextDocumentLayout::update,
        this, [this] { ++m_counts[QStringLiteral("layoutUpdate")]; }));
    m_connections.append(connect(layout, &QAbstractTextDocumentLayout::updateBlock,
        this, [this] { ++m_counts[QStringLiteral("layoutUpdateBlock")]; }));
}

QString DocumentProbe::longText() const
{
    QString text;
    text.reserve(1600000);
    for (int paragraph = 0; paragraph < 10000; ++paragraph) {
        text += QStringLiteral("Paragraph %1. This deterministic editable document keeps its contents, selection, undo history and scroll while appearance colors change.\n")
            .arg(paragraph, 5, 10, QLatin1Char('0'));
    }
    return text;
}

QVariantMap DocumentProbe::snapshot() const
{
    if (!m_document)
        return {};
    const auto text = m_document->toPlainText();
    const auto size = m_document->documentLayout()->documentSize();
    QVariantMap format;
    const auto properties = m_document->firstBlock().charFormat().properties();
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        format.insert(QString::number(it.key()), it.value());
    return {{QStringLiteral("identity"), QVariant::fromValue(quintptr(m_document.data()))},
        {QStringLiteral("layoutIdentity"), QVariant::fromValue(quintptr(m_document->documentLayout()))},
        {QStringLiteral("revision"), m_document->revision()},
        {QStringLiteral("characters"), m_document->characterCount()},
        {QStringLiteral("paragraphs"), m_document->blockCount()},
        {QStringLiteral("textSha256"), QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("font"), m_document->defaultFont()},
        {QStringLiteral("firstFormat"), format},
        {QStringLiteral("undo"), m_document->isUndoAvailable()},
        {QStringLiteral("redo"), m_document->isRedoAvailable()},
        {QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()}};
}
