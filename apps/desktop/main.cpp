#include <QApplication>
#include <QLabel>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QLabel label("CoreDesk desktop placeholder: M0 engineering skeleton only");
    label.resize(480, 80);
    label.show();
    return QApplication::exec();
}
