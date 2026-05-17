#pragma once

#include <QtWidgets/QMainWindow>
#include <QTreeWidget>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QTcpSocket>
#include "ui_DBMS_Client.h"

class DBMS_Client : public QMainWindow
{
    Q_OBJECT

public:
    DBMS_Client(QWidget* parent = nullptr);
    ~DBMS_Client();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError socketError);

    // 用户与控制台管理槽函数
    void showTreeContextMenu(const QPoint& pos);
    void handleRegisterUser();
    void handleOpenConsole(const QString& username);

    void onTreeItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    Ui::DBMS_ClientClass ui;
    QTcpSocket* tcpSocket;

    //新增：会话状态变量
    QString m_currentActiveUser; // 记录当前打开的控制台绑定了哪个用户
};
