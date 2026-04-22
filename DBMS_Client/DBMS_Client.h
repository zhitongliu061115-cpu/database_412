#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_DBMS_Client.h"

class DBMS_Client : public QMainWindow
{
    Q_OBJECT

public:
    DBMS_Client(QWidget *parent = nullptr);
    ~DBMS_Client();

private:
    Ui::DBMS_ClientClass ui;
};

