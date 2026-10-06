#pragma once

#include <QWidget>

class BgfxWindow : public QWidget
{
    Q_OBJECT

public:
    explicit BgfxWindow(QWidget* parent = nullptr);
    ~BgfxWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    bool initBgfx();
    void shutdownBgfx();
    void renderFrame();

private:
    bool m_bgfxInitialized = false;
};
