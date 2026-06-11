#include "prompterwidget.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QRegularExpression>
#include <QStringList>
#include <QWheelEvent>

namespace {

constexpr int kHorizontalPadding = 40;
constexpr int kVerticalFontSize = 54;
constexpr int kVerticalStepPadding = 10;
constexpr int kVerticalColumnGap = 48;
constexpr int kTitleColumnGap = 60;
constexpr int kHorizontalDefaultFontSize = 60;
constexpr int kHorizontalLineGap = 18;
constexpr int kTitleGlyphGap = 18;
constexpr int kMinHorizontalFontSize = 18;
constexpr int kMaxHorizontalFontSize = 200;
const QString kFontFamily = QStringLiteral("SimSun");
const QRegularExpression kSizeTagPattern(
    QStringLiteral(R"(^(?:size|font|s)\s*=\s*(\d+)$)"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kShortSizeTagPattern(QStringLiteral(R"(^(\d+)$)"));
const QStringList kClosingTags{
    QStringLiteral("/"),
    QStringLiteral("/size"),
    QStringLiteral("/font"),
    QStringLiteral("/s")
};

struct RichGlyph {
    QString value;
    int fontSize = kHorizontalDefaultFontSize;
    int advance = 0;
    int height = 0;
    int ascent = 0;
};

using RichLine = QVector<RichGlyph>;
using RichBlock = QVector<RichLine>;
using RichColumn = QVector<RichGlyph>;

struct RichMetrics {
    QVector<int> lineWidths;
    QVector<int> lineHeights;
    int width = 0;
    int height = 0;
};

struct VerticalColumnMetrics {
    int width = 0;
    int height = 0;
};

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

int clampColumnGap(int value)
{
    return qBound(0, value, 400);
}

QVector<int> parseColumnGaps(const QJsonValue &value)
{
    QVector<int> gaps;
    if (!value.isArray()) {
        return gaps;
    }

    const QJsonArray array = value.toArray();
    gaps.reserve(array.size());
    for (const QJsonValue &entry : array) {
        gaps.append(clampColumnGap(entry.toInt(0)));
    }

    return gaps;
}

bool isClosingTag(const QString &tag)
{
    return kClosingTags.contains(tag.toLower());
}

bool tryParseTagFontSize(const QString &rawTag, int *fontSize)
{
    QRegularExpressionMatch match = kSizeTagPattern.match(rawTag);
    if (!match.hasMatch()) {
        match = kShortSizeTagPattern.match(rawTag);
    }

    if (!match.hasMatch()) {
        return false;
    }

    *fontSize = clampHorizontalFontSize(match.captured(1).toInt());
    return true;
}

PrompterWidget::PromptMode parseMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("horizontal")
        || normalized == QStringLiteral("landscape")
        || normalized == QStringLiteral("横向")
        || normalized == QStringLiteral("横排")) {
        return PrompterWidget::PromptMode::Horizontal;
    }

    if (normalized == QStringLiteral("title")
        || normalized == QStringLiteral("headline")
        || normalized == QStringLiteral("标题")
        || normalized == QStringLiteral("標題")) {
        return PrompterWidget::PromptMode::Title;
    }

    return PrompterWidget::PromptMode::Vertical;
}

RichBlock parseRichBlock(const QString &text, int defaultFontSize)
{
    RichBlock lines(1);
    QVector<int> sizeStack{clampHorizontalFontSize(defaultFontSize)};
    int currentFontSize = sizeStack.constLast();
    int index = 0;

    while (index < text.size()) {
        if (text.at(index) == QLatin1Char('<')) {
            const int closeIndex = text.indexOf(QLatin1Char('>'), index);
            if (closeIndex > index) {
                const QString rawTag = text.mid(index + 1, closeIndex - index - 1).trimmed();
                if (isClosingTag(rawTag)) {
                    if (sizeStack.size() > 1) {
                        sizeStack.removeLast();
                    }
                    currentFontSize = sizeStack.constLast();
                    index = closeIndex + 1;
                    continue;
                }

                int tagFontSize = 0;
                if (tryParseTagFontSize(rawTag, &tagFontSize)) {
                    currentFontSize = tagFontSize;
                    sizeStack.append(currentFontSize);
                    index = closeIndex + 1;
                    continue;
                }
            }
        }

        const QChar currentChar = text.at(index);
        if (currentChar == QLatin1Char('\r')) {
            ++index;
            continue;
        }

        if (currentChar == QLatin1Char('\n')) {
            lines.append(RichLine{});
            ++index;
            continue;
        }

        RichGlyph glyph;
        glyph.value = QString(currentChar);
        glyph.fontSize = currentFontSize;
        lines.last().append(glyph);
        ++index;
    }

    while (lines.size() > 1 && lines.constLast().isEmpty()) {
        lines.removeLast();
    }

    return lines;
}

RichColumn flattenRichBlockToColumn(const RichBlock &block)
{
    RichColumn column;
    for (const RichLine &line : block) {
        for (const RichGlyph &glyph : line) {
            column.append(glyph);
        }
    }
    return column;
}

RichMetrics measureRichBlock(RichBlock &block, const QString &fontFamily, int fallbackFontSize, int lineGap)
{
    RichMetrics metrics;

    QFont fallbackFont(fontFamily);
    fallbackFont.setPixelSize(clampHorizontalFontSize(fallbackFontSize));
    fallbackFont.setBold(true);
    const QFontMetrics fallbackMetrics(fallbackFont);

    metrics.lineWidths.reserve(block.size());
    metrics.lineHeights.reserve(block.size());

    for (RichLine &line : block) {
        if (line.isEmpty()) {
            metrics.lineWidths.append(fallbackMetrics.horizontalAdvance(QStringLiteral(" ")));
            metrics.lineHeights.append(fallbackMetrics.height());
            metrics.width = qMax(metrics.width, metrics.lineWidths.constLast());
            metrics.height += metrics.lineHeights.constLast();
            continue;
        }

        int lineWidth = 0;
        int lineHeight = 0;

        for (RichGlyph &glyph : line) {
            QFont glyphFont(fontFamily);
            glyphFont.setPixelSize(clampHorizontalFontSize(glyph.fontSize));
            glyphFont.setBold(true);
            const QFontMetrics glyphMetrics(glyphFont);

            glyph.advance = glyphMetrics.horizontalAdvance(glyph.value);
            glyph.height = glyphMetrics.height();
            glyph.ascent = glyphMetrics.ascent();

            lineWidth += glyph.advance;
            lineHeight = qMax(lineHeight, glyph.height);
        }

        metrics.lineWidths.append(lineWidth);
        metrics.lineHeights.append(lineHeight);
        metrics.width = qMax(metrics.width, lineWidth);
        metrics.height += lineHeight;
    }

    if (metrics.lineHeights.size() > 1) {
        metrics.height += (metrics.lineHeights.size() - 1) * lineGap;
    }

    return metrics;
}

VerticalColumnMetrics measureVerticalColumn(RichColumn &column, const QString &fontFamily,
                                            int fallbackFontSize, int glyphGap)
{
    VerticalColumnMetrics metrics;

    QFont fallbackFont(fontFamily);
    fallbackFont.setPixelSize(clampHorizontalFontSize(fallbackFontSize));
    fallbackFont.setBold(true);
    const QFontMetrics fallbackMetrics(fallbackFont);

    if (column.isEmpty()) {
        metrics.width = qMax(fallbackMetrics.horizontalAdvance(QStringLiteral("字")), fallbackFontSize) + 8;
        metrics.height = fallbackMetrics.height();
        return metrics;
    }

    for (RichGlyph &glyph : column) {
        QFont glyphFont(fontFamily);
        glyphFont.setPixelSize(clampHorizontalFontSize(glyph.fontSize));
        glyphFont.setBold(true);
        const QFontMetrics glyphMetrics(glyphFont);

        glyph.advance = glyphMetrics.horizontalAdvance(glyph.value);
        glyph.height = glyphMetrics.height();
        glyph.ascent = glyphMetrics.ascent();

        metrics.width = qMax(metrics.width, qMax(glyph.advance, glyph.fontSize) + 8);
        metrics.height += glyph.height;
    }

    if (column.size() > 1) {
        metrics.height += (column.size() - 1) * glyphGap;
    }

    return metrics;
}

void drawRichBlock(QPainter &painter, const RichBlock &block, const RichMetrics &metrics,
                   const QRect &rect, int lineGap)
{
    int y = rect.center().y() - metrics.height / 2;

    for (int lineIndex = 0; lineIndex < block.size(); ++lineIndex) {
        const RichLine &line = block.at(lineIndex);
        const int lineWidth = metrics.lineWidths.at(lineIndex);
        const int lineHeight = metrics.lineHeights.at(lineIndex);
        int x = rect.center().x() - lineWidth / 2;

        for (const RichGlyph &glyph : line) {
            QFont glyphFont(kFontFamily);
            glyphFont.setPixelSize(clampHorizontalFontSize(glyph.fontSize));
            glyphFont.setBold(true);
            painter.setFont(glyphFont);

            const QFontMetrics glyphMetrics(glyphFont);
            const int baseline = y + (lineHeight - glyphMetrics.height()) / 2 + glyphMetrics.ascent();
            painter.drawText(x, baseline, glyph.value);
            x += glyph.advance;
        }

        y += lineHeight + lineGap;
    }
}

void drawVerticalColumn(QPainter &painter, const RichColumn &column, const VerticalColumnMetrics &metrics,
                        const QRect &rect, int glyphGap)
{
    int y = rect.center().y() - metrics.height / 2;

    for (const RichGlyph &glyph : column) {
        QFont glyphFont(kFontFamily);
        glyphFont.setPixelSize(clampHorizontalFontSize(glyph.fontSize));
        glyphFont.setBold(true);
        painter.setFont(glyphFont);

        const QRect glyphRect(rect.x(), y, metrics.width, glyph.height);
        painter.drawText(glyphRect, Qt::AlignCenter, glyph.value);
        y += glyph.height + glyphGap;
    }
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
        QDir(appDir).filePath(QStringLiteral("../text/text.json")),
        QDir(appDir).filePath(QStringLiteral("../text.json")),
        QDir::current().filePath(QStringLiteral("text/text.json")),
        QDir::current().filePath(QStringLiteral("text.json")),
        QDir(appDir).filePath(QStringLiteral("text.json"))
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
    const bool hadValidItems = !m_items.isEmpty();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (!hadValidItems) {
            m_items.clear();
            m_currentIndex = 0;
            m_statusMessage = QStringLiteral("无法打开 text.json");
            update();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (!hadValidItems) {
            m_items.clear();
            m_currentIndex = 0;
            m_statusMessage = QStringLiteral("text.json 解析失败：%1").arg(parseError.errorString());
            update();
        }
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
        if (!hadValidItems) {
            m_items.clear();
            m_currentIndex = 0;
            m_statusMessage = QStringLiteral("text.json 顶层需要是数组或对象。");
            update();
        }
        return false;
    }

    QVector<PromptItem> loadedItems;
    loadedItems.reserve(itemsArray.size());
    const int previousIndex = m_currentIndex;

    for (const QJsonValue &value : itemsArray) {
        PromptItem item;

        if (value.isString()) {
            item.text = normalizeText(value.toString());
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            item.text = normalizeText(readTextValue(object));
            item.mode = parseMode(
                object.value(QStringLiteral("mode")).toString(
                    object.value(QStringLiteral("orientation")).toString(
                        object.value(QStringLiteral("方向")).toString())));
            item.fontSize = clampHorizontalFontSize(
                object.value(QStringLiteral("fontSize")).toInt(
                    object.value(QStringLiteral("字号")).toInt(kHorizontalDefaultFontSize)));
            item.columnGap = clampColumnGap(
                object.value(QStringLiteral("columnGap")).toInt(
                    object.value(QStringLiteral("titleColumnGap")).toInt(
                        object.value(QStringLiteral("列间距")).toInt(kTitleColumnGap))));
            item.columnGaps = parseColumnGaps(
                object.value(QStringLiteral("columnGaps")).isArray()
                    ? object.value(QStringLiteral("columnGaps"))
                    : object.value(QStringLiteral("titleColumnGaps")).isArray()
                        ? object.value(QStringLiteral("titleColumnGaps"))
                        : object.value(QStringLiteral("列间距组")));
        }

        if (!item.text.trimmed().isEmpty()) {
            loadedItems.append(item);
        }
    }

    m_items = loadedItems;
    m_verticalBreakMarker = breakMarker;
    m_loadedFilePath = QFileInfo(filePath).absoluteFilePath();
    m_loadedFileLastModified = QFileInfo(m_loadedFilePath).lastModified();

    if (m_items.isEmpty()) {
        if (!hadValidItems) {
            m_currentIndex = 0;
            m_statusMessage = QStringLiteral("text.json 里没有可显示的文案。");
            update();
        }
        return false;
    }

    m_currentIndex = qBound(0, previousIndex, m_items.size() - 1);
    m_statusMessage.clear();
    update();
    return true;
}

void PrompterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    reloadIfFileChanged();

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

void PrompterWidget::reloadIfFileChanged()
{
    if (m_loadedFilePath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(m_loadedFilePath);
    if (!fileInfo.exists()) {
        return;
    }

    const QDateTime lastModified = fileInfo.lastModified();
    if (m_loadedFileLastModified.isValid() && lastModified <= m_loadedFileLastModified) {
        return;
    }

    loadPromptFile(m_loadedFilePath);
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
    switch (item.mode) {
    case PromptMode::Horizontal:
        paintHorizontal(painter, item, rect);
        return;
    case PromptMode::Title:
        paintTitle(painter, item, rect);
        return;
    case PromptMode::Vertical:
        paintVertical(painter, item, rect);
        return;
    }
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
    RichBlock block = parseRichBlock(item.text, item.fontSize);
    const RichMetrics metrics = measureRichBlock(block, kFontFamily, item.fontSize, kHorizontalLineGap);
    const QRect blockRect(rect.center().x() - metrics.width / 2,
                          rect.center().y() - metrics.height / 2,
                          metrics.width,
                          metrics.height);
    drawRichBlock(painter, block, metrics, blockRect, kHorizontalLineGap);
}

void PrompterWidget::paintTitle(QPainter &painter, const PromptItem &item, const QRect &rect)
{
    const QVector<QString> columns = splitTitleColumns(item.text);
    if (columns.isEmpty()) {
        return;
    }

    QVector<RichColumn> richColumns;
    QVector<VerticalColumnMetrics> metricsList;
    richColumns.reserve(columns.size());
    metricsList.reserve(columns.size());

    int totalWidth = 0;
    for (const QString &columnText : columns) {
        const RichBlock block = parseRichBlock(columnText, item.fontSize);
        RichColumn column = flattenRichBlockToColumn(block);
        VerticalColumnMetrics metrics = measureVerticalColumn(column, kFontFamily, item.fontSize, kTitleGlyphGap);
        totalWidth += metrics.width;
        richColumns.append(column);
        metricsList.append(metrics);
    }

    for (int inputIndex = richColumns.size() - 1; inputIndex > 0; --inputIndex) {
        const int gapIndex = inputIndex - 1;
        const int gap = gapIndex < item.columnGaps.size()
            ? item.columnGaps.at(gapIndex)
            : item.columnGap;
        totalWidth += gap;
    }

    int x = rect.center().x() - totalWidth / 2;

    for (int columnIndex = richColumns.size() - 1; columnIndex >= 0; --columnIndex) {
        const RichColumn &column = richColumns.at(columnIndex);
        const VerticalColumnMetrics &metrics = metricsList.at(columnIndex);
        const QRect columnRect(x,
                               rect.center().y() - metrics.height / 2,
                               metrics.width,
                               metrics.height);
        drawVerticalColumn(painter, column, metrics, columnRect, kTitleGlyphGap);
        x += metrics.width;
        if (columnIndex > 0) {
            const int gapIndex = columnIndex - 1;
            const int gap = gapIndex < item.columnGaps.size()
                ? item.columnGaps.at(gapIndex)
                : item.columnGap;
            x += gap;
        }
    }
}

QVector<QString> PrompterWidget::splitTitleColumns(const QString &text) const
{
    QString normalized = text;
    normalized.remove(QLatin1Char('\r'));

    QVector<QString> columns;
    const QStringList segments = normalized.split(QLatin1Char('$'), Qt::KeepEmptyParts);
    columns.reserve(segments.size());
    for (const QString &segment : segments) {
        columns.append(segment);
    }

    if (columns.isEmpty()) {
        columns.append(QString());
    }

    return columns;
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
    text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    return text;
}

bool PrompterWidget::isHorizontalMode(const QString &mode)
{
    return parseMode(mode) == PromptMode::Horizontal;
}
