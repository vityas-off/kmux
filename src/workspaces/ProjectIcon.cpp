/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "workspaces/ProjectIcon.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QSaveFile>
#include <QStandardPaths>

#include <KLocalizedString>

namespace
{
class SymbolicIconEngine : public QIconEngine
{
public:
    explicit SymbolicIconEngine(const QString &path, const QColor &color)
        : _source(path)
        , _color(color)
    {
    }

    QIconEngine *clone() const override
    {
        return new SymbolicIconEngine(*this);
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale = painter->device()->devicePixelRatioF();
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result = _source.pixmap(size, scale, QIcon::Normal, state);
        if (!result.isNull()) {
            const QPalette palette = QApplication::palette();
            const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
            const auto role = mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::Text;
            QPainter painter(&result);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(result.rect(), _color.isValid() ? _color : palette.color(group, role));
        }
        return result;
    }

private:
    QIcon _source;
    QColor _color;
};
}

QIcon Konsole::ProjectIcon::icon(const QString &name, const QColor &color)
{
    const QIcon fallback(new SymbolicIconEngine(QStringLiteral(":/project-icons/material/folder.svg"), color));
    if (name.isEmpty()) {
        return fallback;
    }
    if (name.startsWith(QLatin1String(":/project-icons/material/")) || name.startsWith(QLatin1String(":/project-icons/devicon/monochrome/"))) {
        return QFile::exists(name) ? QIcon(new SymbolicIconEngine(name, color)) : fallback;
    }
    if (name.startsWith(QLatin1String(":/project-icons/devicon/"))) {
        return QFile::exists(name) ? QIcon(name) : fallback;
    }
    if (QDir::isAbsolutePath(name)) {
        const QIcon custom(name);
        return custom.pixmap(32).isNull() ? fallback : custom;
    }
    return QIcon::fromTheme(name, fallback);
}

QString Konsole::ProjectIcon::importFile(const QString &path, QString &error)
{
    error.clear();
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        error = source.errorString();
        return {};
    }
    const QByteArray data = source.readAll();
    if (source.error() != QFileDevice::NoError) {
        error = source.errorString();
        return {};
    }
    if (QIcon(path).pixmap(32).isNull()) {
        error = i18nc("@info", "This file cannot be used as an icon.");
        return {};
    }

    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/project-icons");
    if (!QDir().mkpath(directory)) {
        error = i18nc("@info", "Could not create the project icon directory.");
        return {};
    }

    const QString hash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    const QString destination = directory + QLatin1Char('/') + hash + QLatin1Char('.') + QFileInfo(path).suffix().toLower();
    if (QFile::exists(destination)) {
        return destination;
    }
    QSaveFile target(destination);
    if (!target.open(QIODevice::WriteOnly) || target.write(data) != data.size() || !target.commit()) {
        error = target.errorString();
        return {};
    }
    return destination;
}
