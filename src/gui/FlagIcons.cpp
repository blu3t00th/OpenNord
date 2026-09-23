#include "gui/FlagIcons.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace opennord {
namespace {
QString normalizedCode(const QString &value)
{
    auto code = value.trimmed().toLower();
    if (code == QStringLiteral("uk")) code = QStringLiteral("gb");
    if (code == QStringLiteral("el")) code = QStringLiteral("gr");
    if (code.size() != 2 || code[0] < u'a' || code[0] > u'z' || code[1] < u'a' || code[1] > u'z') return {};
    return code;
}

QString resourcePath(const QString &code)
{
    return QStringLiteral(":/flags/%1.png").arg(code);
}
}

bool hasCountryFlag(const QString &countryCode)
{
    const auto code = normalizedCode(countryCode);
    return !code.isEmpty() && QFile::exists(resourcePath(code));
}

QIcon countryFlagIcon(const QString &countryCode)
{
    // GUI-thread cache: repeated city rows share one sharp, circular flag icon.
    static QHash<QString, QIcon> cache;
    const auto code = hasCountryFlag(countryCode) ? normalizedCode(countryCode) : QString{};
    const auto cached = cache.constFind(code);
    if (cached != cache.cend()) return cached.value();

    QPixmap image(96, 96);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds(1, 1, 94, 94);
    QPainterPath circle;
    circle.addEllipse(bounds);
    painter.setClipPath(circle);
    const QPixmap flag(code.isEmpty() ? QString{} : resourcePath(code));
    if (!flag.isNull()) {
        painter.drawPixmap(bounds, flag, flag.rect());
    } else {
        // Unknown/future codes get a neutral globe, never a made-up national flag.
        painter.fillRect(image.rect(), QColor(QStringLiteral("#343d39")));
        painter.setPen(QPen(QColor(QStringLiteral("#d8e8ce")), 3));
        painter.drawEllipse(QRectF(22, 22, 52, 52));
        painter.drawEllipse(QRectF(36, 22, 24, 52));
        painter.drawLine(QPointF(22, 48), QPointF(74, 48));
    }
    painter.setClipping(false);
    painter.setPen(QPen(QColor(255, 255, 255, 28), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(bounds);
    painter.end();
    image.setDevicePixelRatio(3);
    const QIcon icon(image);
    cache.insert(code, icon);
    return icon;
}

}
