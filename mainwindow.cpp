#include "mainwindow.h"
#include "bgfxwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    QObject::connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::onPushButtonClicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onPushButtonClicked()
{
	auto* window = new BgfxWindow(nullptr);

	window->setAttribute(Qt::WA_DeleteOnClose);

	window->setWindowTitle("BGFX Viewport");

	window->resize(1280, 720);

	window->show();
}
