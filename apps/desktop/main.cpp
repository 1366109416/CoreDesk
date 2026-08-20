#include <QApplication>

#include "MainWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    coredesk::ui::MainWindow window;
    window.show();
    return QApplication::exec();
}
