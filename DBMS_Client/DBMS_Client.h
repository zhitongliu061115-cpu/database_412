#pragma once

#include <QtWidgets/QMainWindow>
#include <QTcpSocket>
#include <QAbstractSocket>
#include "ui_DBMS_Client.h"

class DBMS_Client : public QMainWindow
{
    Q_OBJECT

public:
    DBMS_Client(QWidget *parent = nullptr);
    ~DBMS_Client();

private slots:
    void onConnectClicked();
    void onRunClicked();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);

private:
    void appendLog(const QString& line);
    void renderResult(const QString& responseText);

    Ui::DBMS_ClientClass ui;
    QTcpSocket* socket_;
    QByteArray recvBuffer_;
    qint32 pendingPayloadBytes_;
};

