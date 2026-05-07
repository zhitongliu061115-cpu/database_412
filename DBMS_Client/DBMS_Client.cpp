#include "DBMS_Client.h"
#include <QDateTime>
#include <QMessageBox>

DBMS_Client::DBMS_Client(QWidget* parent)
    : QMainWindow(parent), tcpSocket(new QTcpSocket(this))
{
    ui.setupUi(this);

    ui.sqlEditor->setText("CREATE DATABASE testDB;");
    ui.treeWidget->setHeaderLabel("对象资源管理器");
    ui.logBrowser->append("[系统] 客户端已启动。");

    // 绑定 Socket 的信号与槽
    connect(tcpSocket, &QTcpSocket::connected, this, &DBMS_Client::onConnected);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &DBMS_Client::onReadyRead);
    connect(tcpSocket, &QTcpSocket::errorOccurred, this, &DBMS_Client::onError);

    // 连接按钮
    connect(ui.btnConnect, &QPushButton::clicked, this, [=]() {
        // 1. 检查是否已经连上了
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            ui.logBrowser->append("<font color='orange'>[提示] 已经连接到服务端，无需重复连接！您现在可以直接执行 SQL 了。</font>");
            return; // 已经连上，直接退出函数，不再重复发起连接
        }

        // 2. 检查是否正在努力连接中
        if (tcpSocket->state() == QAbstractSocket::ConnectingState) {
            ui.logBrowser->append("<font color='orange'>[提示] 正在努力连接中，请勿频繁点击...</font>");
            return;
        }

        // 3. 在发起新连接前，先粗暴地切断以前可能残留的“僵尸连接”或错误状态
        tcpSocket->abort();

        // 4. 正式发起连接
        ui.logBrowser->append("[网络] 尝试连接到 127.0.0.1:8080 ...");
        tcpSocket->connectToHost("127.0.0.1", 8080);
        });

    // 执行按钮
    connect(ui.btnRun, &QPushButton::clicked, this, [=]() {
        QString sqlText = ui.sqlEditor->toPlainText().trimmed();
        if (sqlText.isEmpty()) return;

        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            // 发送给服务端 (UTF-8 编码)
            tcpSocket->write(sqlText.toUtf8());
            ui.logBrowser->append("<font color='white'><b>[发送]</b> " + sqlText + "</font>");
        }
        else {
            QMessageBox::warning(this, "错误", "尚未连接到服务端！");
        }
        });
}

DBMS_Client::~DBMS_Client()
{
    if (tcpSocket->isOpen()) {
        tcpSocket->close();
    }
}

void DBMS_Client::onConnected()
{
    ui.logBrowser->append("<font color='yellow'>[网络] 成功连接到 DBMS 服务端！</font>");
}

void DBMS_Client::onReadyRead()
{
    // 读取服务端返回的数据
    QByteArray data = tcpSocket->readAll();
    QString response = QString::fromUtf8(data).trimmed();

    // 显示在下方的日志框里
    ui.logBrowser->append("<font color='#00ff00'>[回执] " + response + "</font>");
}

void DBMS_Client::onError(QAbstractSocket::SocketError socketError)
{
    ui.logBrowser->append("<font color='red'>[错误] 网络连接异常: " + tcpSocket->errorString() + "</font>");
}