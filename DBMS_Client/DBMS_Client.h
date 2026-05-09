#pragma once

#include <QtWidgets/QMainWindow>
#include <QTcpSocket>
#include "ui_DBMS_Client.h"

class DBMS_Client : public QMainWindow
{
    Q_OBJECT

public:
    DBMS_Client(QWidget* parent = nullptr);
    ~DBMS_Client();

protected:
    // 拦截键盘事件的过滤器
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError socketError);

private:
    Ui::DBMS_ClientClass ui;
    QTcpSocket* tcpSocket;
};