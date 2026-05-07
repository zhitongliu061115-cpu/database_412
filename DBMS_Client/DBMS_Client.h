#pragma once

#include <QtWidgets/QMainWindow>
#include <QTcpSocket>  // 引入 Socket
#include "ui_DBMS_Client.h"

class DBMS_Client : public QMainWindow
{
    Q_OBJECT

public:
    DBMS_Client(QWidget* parent = nullptr);
    ~DBMS_Client();

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError socketError);

private:
    Ui::DBMS_ClientClass ui;
    QTcpSocket* tcpSocket; // 添加网络套接字指针
};