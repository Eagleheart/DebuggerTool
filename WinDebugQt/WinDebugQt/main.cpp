#include <QtWidgets/QApplication>

#include "DebugHandler.h"
#include "WinDebugQtPresenter.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    DebugHandler dh;
    WinDebugQtPresenter w(dh);
    w.show();

    return a.exec();
}