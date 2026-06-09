#include "prompterwidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

namespace {

constexpr int kHorizontalPadding = 40;
constexpr int kVerticalFontSize = 54;
constexpr int kVerticalStepPadding = 10;
constexpr int kVerticalColumnGap = 30;
constexpr int kHorizontalDefaultFontSize = 60;
constexpr int kHorizontalLineGap = 18;
constexpr int kMinHorizontalFontSize = 18;
constexpr int kMaxHorizontalFontSize = 200;
const QString kFontFamily = QStringLiteral("Microsoft YaHei UI");

QString readTextValue(const QJsonObject &object)
{
    if (object.value(QStringLiteral("text")).isString()) {
        return object.value(QStringLiteral("text")).toString();
    }
    if (object.value(QStringLiteral("content")).isString()) {
        return object.value(QStringLiteral("content")).toString();
    }
    if (object.value(QStringLiteral("内容")).isString()) {
        return object.value(QStringLiteral("内容")).toString();
    }
    return {};
}

int clampHorizontalFontSize(int value)
{
    return qBound(kMinHorizontalFontSize, value, kMaxHorizontalFontSize);
}

} // namespace

PrompterWidget::PrompterWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
}

bool PrompterWidget::loadDefaultPromptFile()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir(appDir).filePath(QStringLiteral("text.json")),
        QDir(appDir).filePath(QStringLiteral("../text.json")),
        QDir::current().filePath(QStringLiteral("text.json"))
    };

    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            return loadPromptFile(candidate);
        }
    }

    m_items.clear();
    m_currentIndex = 0;
    m_statusMessage = QStringLiteral("未找到 text.json\n请把文件放到程序同目录。");
    update();
    return false;
}

bool PrompterWidget::loadPromptFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_items.clear();
        m_currentIndex = 0;
        m_statusMessage = QStringLiteral("无法打开 text.json");
        update();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        m_items.clear();
        m_currentIndex = 0;
        m_statusMessage = QStringLiteral("text.json 解析失败：%1").arg(parseError.errorString());
        update();
        return false;
    }

    QJsonArray itemsArray;
    QString breakMarker = QStringLiteral("|");

    if (document.isArray()) {
        itemsArray = document.array();
    } else if (document.isObject()) {
        const QJsonObject rootObject = document.object();
        itemsArray = rootObject.value(QStringLiteral("items")).toArray();

        const QString marker = rootObject.value(QStringLiteral("verticalBreakMarker")).toString().trimmed();
        if (!marker.isEmpty()) {
            breakMarker = marker;
        }
    } else {
        m_items.clear();
        m_currentIndex = 0;
        m_statusMessage = QStringLiteral("text.json 顶层需要是数组或对象。");
        update();
        return false;
    }

    QVector<PromptItem> loadedItems;
    loadedItems.reserve(itemsArray.size());

    for (const QJsonValue &value : itemsArray) {
        PromptItem item;

        if (value.isString()) {
            item.text = normalizeText(value.toString());
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            item.text = normalizeText(readTextValue(object));
            item.horizontal = isHorizontalMode(
                object.value(QStringLiteral("mode")).toString(
                    object.value(QStringLiteral("orientation")).toString(
                        object.value(QStringLiteral("方向")).toString())));
            item.fontSize = clampHorizontalFontSize(
                object.value(QStringLiteral("fontSize")).toInt(
                    object.value(QStringLiteral("字号")).toInt(kHorizontalDefaultFontSize)));

            const QJsonArray charSizesArray = object.value(QStringLiteral("charSizes")).toArray();
            item.charSizes.reserve(charSizesArray.size());
            for (const QJsonValue &sizeValue : charSizesArray) {
                item.charSizes.append(clampHorizontalFontSize(sizeValue.toInt(kHorizontalDefaultFontSize)));
            }
        }

        if (!item.text.trimmed().isEmpty()) {
            loadedItems.append(item);
        }
    }

    m_items = loadedItems;
    m_verticalBreakMarker = breakMarker;
    m_currentIndex = 0;

    if (m_items.isEmpty()) {
        m_statusMessage = QStringLiteral("text.json 里没有可显示的文案。");
        update();
        return false;
    }

    m_statusMessage.clear();
    update();
    return true;
}

void PrompterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#000000")));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(QStringLiteral("#ff8c1a")));

    if (!m_statusMessage.isEmpty() || m_items.isEmpty()) {
        paintStatus(painter, m_statusMessage);
        return;
    }

    paintPrompt(painter, rect().adjusted(kHorizontalPadding, kHorizontalPadding,
                                         -kHorizontalPadding, -kHorizontalPadding));
}

void PrompterWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();

    if (event->button() == Qt::LeftButton) {
        nextItem();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void PrompterWidget::wheelEvent(QWheelEvent *event)
{
    setFocus();

    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }

    if (delta < 0) {
        nextItem();
        event->accept();
        return;
    }

    if (delta > 0) {
        previousItem();
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
}

void PrompterWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        nextItem();
        event->accept();
        return;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void PrompterWidget::nextItem()
{
    if (m_items.isEmpty()) {
        return;
    }

    if (m_currentIndex < m_items.size() - 1) {
        ++m_currentIndex;
        update();
    }
}

void PrompterWidget::previousItem()
{
    if (m_items.isEmpty()) {
        return;
    }

    if (m_currentIndex > 0) {
        --m_currentIndex;
        update();
    }
}

void PrompterWidget::paintStatus(QPainter &painter, const QString &message)
{
    QFont font(kFontFamily);
    font.setPixelSize(36);
    font.setBold(true);
    painter.setFont(font);

    const QString fallback = message.isEmpty()
        ? QStringLiteral("没有可显示的内容。")
        : message;
    const QString hint = fallback
        + QStringLiteral("\n\n左键 / 空格 / Enter：下一句\n滚轮上滑：上一句\n滚轮下滑：下一句");

    painter.drawText(rect().adjusted(50, 50, -50, -50),
                     Qt::AlignCenter | Qt::TextWordWrap,
                     hint);
}

void PrompterWidget::paintPrompt(QPainter &painter, const QRect &rect)
{
    const PromptItem &item = m_items.at(m_currentIndex);
    if (item.horizontal) {
        paintHorizontal(painter, item, rect);
        return;
    }

    paintVertical(painter, item, rect);
}

void PrompterWidget::paintVertical(QPainter &painter, const PromptItem &item, const QRect &rect)
{
    QFont font(kFontFamily);
    font.setPixelSize(kVerticalFontSize);
    font.setBold(true);
    painter.setFont(font);

    const QFontMetrics metrics(font);
    const int lineStep = metrics.height() + kVerticalStepPadding;
    const int columnWidth = qMax(metrics.horizontalAdvance(QStringLiteral("字")), kVerticalFontSize) + 8;
    const int maxCharsPerColumn = qMax(1, rect.height() / lineStep);
    const QVector<QString> columns = splitVerticalColumns(item.text, maxCharsPerColumn);

    const int columnCount = columns.size();
    const int totalWidth = columnCount * columnWidth + (columnCount - 1) * kVerticalColumnGap;
    const int startX = rect.center().x() - totalWidth / 2;

    for (int index = 0; index < columnCount; ++index) {
        const QString &columnText = columns.at(index);
        const int visualIndex = columnCount - 1 - index;
        const int columnX = startX + visualIndex * (columnWidth + kVerticalColumnGap);
        const int columnHeight = columnText.size() * lineStep;
        const int startY = rect.center().y() - columnHeight / 2;

        for (int row = 0; row < columnText.size(); ++row) {
            const QRect charRect(columnX, startY + row * lineStep, columnWidth, lineStep);
            painter.drawText(charRect, Qt::AlignCenter, columnText.mid(row, 1));
        }
    }
}

void PrompterWidget::paintHorizontal(QPainter &painter, const PromptItem &item, const QRect &rect)
{
    const QStringList lines = item.text.split(QLatin1Char('\n'));

    struct Glyph {
        QString value;
        int fontSize = kHorizontalDefaultFontSize;
        int advance = 0;
        int height = 0;
        int ascent = 0;
    };

    QVector<QVector<Glyph>> glyphLines;
    QVector<int> lineWidths;
    QVector<int> lineHeights;
    glyphLines.reserve(lines.size());
    lineWidths.reserve(lines.size());
    lineHeights.reserve(lines.size());

    QFont defaultFont(kFontFamily);
    defaultFont.setPixelSize(clampHorizontalFontSize(item.fontSize));
    defaultFont.setBold(true);
    const QFontMetrics defaultMetrics(defaultFont);

    int charIndex = 0;
    int totalHeight = 0;

    for (const QString &line : lines) {
        QVector<Glyph> glyphs;
        glyphs.reserve(line.size());

        int lineWidth = 0;
        int lineHeight = defaultMetrics.height();

        for (int i = 0; i < line.size(); ++i) {
            const QString glyphText = line.mid(i, 1);
            const int glyphFontSize = clampHorizontalFontSize(
                charIndex < item.charSizes.size() ? item.charSizes.at(charIndex) : item.fontSize);

            QFont glyphFont(kFontFamily);
            glyphFont.setPixelSize(glyphFontSize);
            glyphFont.setBold(true);
            const QFontMetrics glyphMetrics(glyphFont);

            Glyph glyph;
            glyph.value = glyphText;
            glyph.fontSize = glyphFontSize;
            glyph.advance = glyphMetrics.horizontalAdvance(glyphText);
            glyph.height = glyphMetrics.height();
            glyph.ascent = glyphMetrics.ascent();

            lineWidth += glyph.advance;
            lineHeight = qMax(lineHeight, glyph.height);
            glyphs.append(glyph);
            ++charIndex;
        }

        glyphLines.append(glyphs);
        lineWidths.append(lineWidth);
        lineHeights.append(lineHeight);
        totalHeight += lineHeight;
    }

    if (!lineHeights.isEmpty()) {
        totalHeight += (lineHeights.size() - 1) * kHorizontalLineGap;
    }

    int y = rect.center().y() - totalHeight / 2;

    for (int lineIndex = 0; lineIndex < glyphLines.size(); ++lineIndex) {
        const QVector<Glyph> &glyphs = glyphLines.at(lineIndex);
        const int lineWidth = lineWidths.at(lineIndex);
        const int lineHeight = lineHeights.at(lineIndex);
        int x = rect.center().x() - lineWidth / 2;

        for (const Glyph &glyph : glyphs) {
            QFont glyphFont(kFontFamily);
            glyphFont.setPixelSize(glyph.fontSize);
            glyphFont.setBold(true);
            painter.setFont(glyphFont);

            const QFontMetrics glyphMetrics(glyphFont);
            const int baseline = y + (lineHeight - glyphMetrics.height()) / 2 + glyphMetrics.ascent();
            painter.drawText(x, baseline, glyph.value);
            x += glyph.advance;
        }

        y += lineHeight + kHorizontalLineGap;
    }
}

QVector<QString> PrompterWidget::splitVerticalColumns(const QString &text, int maxCharsPerColumn) const
{
    QString normalized = text;
    normalized.remove(QLatin1Char('\r'));
    normalized.remove(QLatin1Char('\n'));

    QVector<QString> columns;

    if (!m_verticalBreakMarker.isEmpty()) {
        const int markerIndex = normalized.indexOf(m_verticalBreakMarker);
        if (markerIndex >= 0) {
            const QString firstColumn = normalized.left(markerIndex);
            const QString secondColumn = normalized.mid(markerIndex + m_verticalBreakMarker.size());

            if (!firstColumn.isEmpty()) {
                columns.append(firstColumn);
            }
            if (!secondColumn.isEmpty()) {
                columns.append(secondColumn);
            }

            if (!columns.isEmpty()) {
                return columns;
            }
        }
    }

    if (normalized.size() > maxCharsPerColumn) {
        columns.append(normalized.left(maxCharsPerColumn));
        columns.append(normalized.mid(maxCharsPerColumn));
        return columns;
    }

    columns.append(normalized);
    return columns;
}

QString PrompterWidget::normalizeText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\r"), QStringLiteral("\n"));
    return text;
}

bool PrompterWidget::isHorizontalMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    return normalized == QStringLiteral("horizontal")
        || normalized == QStringLiteral("landscape")
        || normalized == QStringLiteral("横向")
        || normalized == QStringLiteral("横排");
}
