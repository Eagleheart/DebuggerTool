#pragma once

#include "ui_WinDebugQtGUI.h"

#include "IDebugHandler.h"

#include <QtWidgets/QMainWindow>

// Presenter class. Fetches data from the debug handler model and updates the view. Sends input commands to the model.
class WinDebugQtPresenter : public QMainWindow
{
    Q_OBJECT

public:
    WinDebugQtPresenter(IDebugHandler& model, QWidget* const parent = Q_NULLPTR);

private:
    void UpdateTick();

    Ui::WinDebugQtGUIClass m_Ui;
    IDebugHandler& m_Model;

    // Slots are handlers corresponding to buttons in WinDebugQtGUI.ui view.
private slots:
    void on_startTool_clicked();
    void on_stopTool_clicked();
};
