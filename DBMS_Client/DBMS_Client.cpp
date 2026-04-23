#include "DBMS_Client.h"

#include <QDateTime>
#include <QHeaderView>
#include <QHostAddress>
#include <QtEndian>
#include <cstring>

namespace {
constexpr quint16 kServerPort = 9527;
}

DBMS_Client::DBMS_Client(QWidget* parent)
    : QMainWindow(parent), socket_(new QTcpSocket(this)), pendingPayloadBytes_(-1)
{
    ui.setupUi(this);

    ui.sqlEditor->setText("SELECT * FROM Account;");
    ui.treeWidget->setHeaderLabel("对象资源管理器");

    QTreeWidgetItem* dbNode = new QTreeWidgetItem(ui.treeWidget);
    dbNode->setText(0, "Ruanko_DB");

    QTreeWidgetItem* tableNode = new QTreeWidgetItem(dbNode);
    tableNode->setText(0, "Account表");

    ui.treeWidget->expandAll();

    ui.resultTable->setColumnCount(1);
    ui.resultTable->setHorizontalHeaderLabels(QStringList() << "Server Response");
    ui.resultTable->horizontalHeader()->setStretchLastSection(true);

    connect(ui.btnConnect, &QPushButton::clicked, this, &DBMS_Client::onConnectClicked);
    connect(ui.btnRun, &QPushButton::clicked, this, &DBMS_Client::onRunClicked);

    connect(socket_, &QTcpSocket::connected, this, &DBMS_Client::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &DBMS_Client::onSocketDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &DBMS_Client::onSocketReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred, this, &DBMS_Client::onSocketError);

    appendLog("[系统] 客户端已启动，点击“连接”后即可发送 SQL。");
    statusBar()->showMessage("未连接");
}

DBMS_Client::~DBMS_Client()
{
}

void DBMS_Client::onConnectClicked() {
    if (socket_->state() == QAbstractSocket::ConnectedState) {
        appendLog("[系统] 主动断开连接。");
        socket_->disconnectFromHost();
        return;
    }

    if (socket_->state() == QAbstractSocket::ConnectingState) {
        appendLog("[系统] 正在连接中，请稍等...");
        return;
    }

    appendLog("[系统] 正在连接 127.0.0.1:9527 ...");
    statusBar()->showMessage("连接中...");
    socket_->connectToHost(QHostAddress::LocalHost, kServerPort);
}

void DBMS_Client::onRunClicked() {
    // 用户点 Run 的时候，先兜底两件事：必须连上服务端，SQL 不能是空字符串。
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        appendLog("[系统] 请先连接服务端，再执行 SQL。");
        return;
    }

    QString sql = ui.sqlEditor->toPlainText().trimmed();
    if (sql.isEmpty()) {
        appendLog("[系统] SQL 为空，本次不发送。");
        return;
    }

    QByteArray request = sql.toUtf8();
    request.append('\n');

    socket_->write(request);
    appendLog("[客户端] SQL 已发送: " + sql.simplified());
}

void DBMS_Client::onSocketConnected() {
    ui.btnConnect->setText("断开连接");
    statusBar()->showMessage("已连接到服务端");
    appendLog("[网络] 连接成功。你现在执行的 SQL 会直接走 Socket 发到后端。");
}

void DBMS_Client::onSocketDisconnected() {
    ui.btnConnect->setText("连接到本地服务端");
    statusBar()->showMessage("连接已断开");
    appendLog("[网络] 连接已断开。");
}

void DBMS_Client::onSocketReadyRead() {
    recvBuffer_.append(socket_->readAll());

    while (true) {
        if (pendingPayloadBytes_ < 0) {
            if (recvBuffer_.size() < static_cast<int>(sizeof(quint32))) {
                return;
            }

            quint32 netOrderLen = 0;
            std::memcpy(&netOrderLen, recvBuffer_.constData(), sizeof(quint32));
            recvBuffer_.remove(0, static_cast<int>(sizeof(quint32)));
            pendingPayloadBytes_ = static_cast<qint32>(qFromBigEndian(netOrderLen));
        }

        if (recvBuffer_.size() < pendingPayloadBytes_) {
            return;
        }

        // 这里按“长度前缀 + 正文”拆包，能稳住粘包/半包，避免日志一坨糊在一起。
        QByteArray payload = recvBuffer_.left(pendingPayloadBytes_);
        recvBuffer_.remove(0, pendingPayloadBytes_);
        pendingPayloadBytes_ = -1;

        QString response = QString::fromUtf8(payload);
        if (response.trimmed().isEmpty()) {
            response = "(服务端返回空内容)";
        }

        appendLog("[服务端]\n" + response.trimmed());
        renderResult(response);
    }
}

void DBMS_Client::onSocketError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    appendLog("[网络错误] " + socket_->errorString());
}

void DBMS_Client::appendLog(const QString& line) {
    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui.logBrowser->append(QString("[%1] %2").arg(now, line));
}

void DBMS_Client::renderResult(const QString& responseText) {
    QStringList lines = responseText.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        lines << "(空响应)";
    }

    ui.resultTable->clearContents();
    ui.resultTable->setColumnCount(1);
    ui.resultTable->setHorizontalHeaderLabels(QStringList() << "Server Response");
    ui.resultTable->setRowCount(lines.size());

    for (int row = 0; row < lines.size(); ++row) {
        ui.resultTable->setItem(row, 0, new QTableWidgetItem(lines[row]));
    }
}
