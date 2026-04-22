#include "DBMS_Client.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    DBMS_Client window;
    window.show();
    return app.exec();
}
