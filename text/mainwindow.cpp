#include "mainwindow.h"
#include "prompterwidget.h"
#include "ui_mainwindow.h"

#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_prompter(new PrompterWidget(this))
{
    ui->setupUi(this);
    setFixedSize(540, 960);
    setWindowTitle(QStringLiteral("提词器"));

    if (menuBar()) {
        menuBar()->hide();
    }
    if (statusBar()) {
        statusBar()->hide();
    }

    setCentralWidget(m_prompter);
    m_prompter->loadDefaultPromptFile();
    m_prompter->setFocus();
}

MainWindow::~MainWindow()
{
    delete ui;
}
