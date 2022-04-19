#include "WinDebugQtPresenter.h"

#include <QScrollBar>
#include <QTimer> 
#include <string>

WinDebugQtPresenter::WinDebugQtPresenter(IDebugHandler& model, QWidget* const parent)
    : m_Model(model),
    QMainWindow(parent)
{
    m_Ui.setupUi(this);

    QTimer* const timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&WinDebugQtPresenter::UpdateTick));
    timer->start(100);
}

void WinDebugQtPresenter::UpdateTick()
{
    // Get data pending in the model and feed it into the view.
    std::string logData = m_Model.GetLogData();

    if (!logData.empty())
    {
        // Since the model may be inserting sameline text between updates, we cannot use QTextEdit::append as that forces a new line between calls.
        // So, we use QTextEdit::insertPlainText which inserts text at the current cursor position. We want to append to the end of the current text, so we must move the cursor to the end.
        // Since moving the cursor position moves the scrollbar, we must store the original scrollbar value and restore it after (unless autoscroll is on, in which case we want it to scroll to the end anyway).
        QScrollBar* const scrollBar = m_Ui.debugOutput->verticalScrollBar();
        const int prevVal = scrollBar->value();

        m_Ui.debugOutput->moveCursor(QTextCursor::End);
        m_Ui.debugOutput->insertPlainText(logData.c_str());

        if (!m_Ui.autoScroll->isChecked())
        {
            scrollBar->setValue(prevVal);
        }
        else
        {
            // If a lot of text was inserted, we may not be at the end anymore so go to the end again.
            m_Ui.debugOutput->moveCursor(QTextCursor::End);
        }
    }
}

void WinDebugQtPresenter::on_startTool_clicked()
{
    m_Ui.startTool->setDisabled(true);
    m_Ui.debugOutput->clear();

    m_Model.StartButtonPressed();
    m_Ui.stopTool->setDisabled(false);
}

void WinDebugQtPresenter::on_stopTool_clicked()
{
    m_Ui.stopTool->setDisabled(true);
    m_Model.StopButtonPressed();
    m_Ui.startTool->setDisabled(false);
}