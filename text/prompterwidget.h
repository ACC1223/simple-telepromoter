#pragma once

#include <QDateTime>
#include <QVector>
#include <QWidget>

class QPainter;

class PrompterWidget : public QWidget
{
public:
    enum class PromptMode {
        Vertical,
        Horizontal,
        Title,
    };

    explicit PrompterWidget(QWidget *parent = nullptr);

    bool loadDefaultPromptFile();
    bool loadPromptFile(const QString &filePath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct PromptItem {
        QString text;
        PromptMode mode = PromptMode::Vertical;
        int fontSize = 60;
        int columnGap = 60;
        QVector<int> columnGaps;
    };

    QVector<PromptItem> m_items;
    int m_currentIndex = 0;
    QString m_verticalBreakMarker = QStringLiteral("|");
    QString m_statusMessage;
    QString m_loadedFilePath;
    QDateTime m_loadedFileLastModified;

    void nextItem();
    void previousItem();
    void reloadIfFileChanged();
    void paintStatus(QPainter &painter, const QString &message);
    void paintPrompt(QPainter &painter, const QRect &rect);
    void paintVertical(QPainter &painter, const PromptItem &item, const QRect &rect);
    void paintHorizontal(QPainter &painter, const PromptItem &item, const QRect &rect);
    void paintTitle(QPainter &painter, const PromptItem &item, const QRect &rect);
    QVector<QString> splitVerticalColumns(const QString &text, int maxCharsPerColumn) const;
    QVector<QString> splitTitleColumns(const QString &text) const;

    static QString normalizeText(QString text);
    static bool isHorizontalMode(const QString &mode);
};
