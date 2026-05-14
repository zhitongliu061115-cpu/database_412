#include "DBMS_Client.h"
#include <QDateTime>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QKeyEvent>
#include <map>

DBMS_Client::DBMS_Client(QWidget* parent)
    : QMainWindow(parent), tcpSocket(new QTcpSocket(this))
{
    ui.setupUi(this);

    this->setStyleSheet(
        // 主窗口背景色
        "QMainWindow { background-color: #F3F4F6; }"

        // 树形列表、编辑器、表格的通用样式：纯白背景、圆角、浅色边框
        "QTreeWidget, QTextEdit, QTableWidget { "
        "   background-color: #FFFFFF; "
        "   border: 1px solid #E5E7EB; "
        "   border-radius: 6px; "
        "   padding: 4px; "
        "   selection-background-color: #3B82F6; " // 选中文本时的蓝色
        "}"

        // 表格表头样式美化
        "QHeaderView::section { "
        "   background-color: #F9FAFB; "
        "   color: #374151; "
        "   border: none; "
        "   border-bottom: 1px solid #E5E7EB; "
        "   padding: 6px; "
        "   font-weight: bold; "
        "}"

        // 普通按钮（导出、连接）的扁平化样式
        "QPushButton { "
        "   background-color: #FFFFFF; "
        "   border: 1px solid #D1D5DB; "
        "   border-radius: 4px; "
        "   color: #374151; "
        "   padding: 6px 12px; "
        "}"
        "QPushButton:hover { background-color: #F3F4F6; }"
        "QPushButton:pressed { background-color: #E5E7EB; }"

        // 执行按钮（绿色主按钮）的特殊样式
        "QPushButton#btnRun { "
        "   background-color: #10B981; "
        "   color: white; "
        "   border: none; "
        "   font-weight: bold; "
        "}"
        "QPushButton#btnRun:hover { background-color: #059669; }"
    );

    // 顺手把底部的黑框再调整得更极客一点
    ui.logBrowser->setStyleSheet(
        "background-color: #1E1E1E; "
        "color: #10B981; " // 荧光绿文字
        "border-radius: 6px; "
        "font-family: Consolas;"
    );

    ui.treeWidget->setStyleSheet(
        "QTreeWidget { "
        "   border-image: url(:/icons/Tree.jpg); " 
        "   border: none; "                        
        "   background-color: transparent; "       
        "   padding: 0px; "
        "   color: #333333; "
        "}"
        "QTreeWidget::item { "
        "   background: transparent; "
        "   height: 25px; "
        "}"
        "QTreeWidget::item:selected { "
        "   background-color: rgba(59, 130, 246, 0.6); " // 选中的蓝色改成半透明，视觉更柔和
        "   color: white; "
        "}"
    );

    ui.sqlEditor->setStyleSheet(
        "QTextEdit { "
        "   border-image: url(:/icons/sql.png); "
        "   background-color: rgba(255, 255, 255, 0.85); "
        "   color: #333333; "
        "}"
    );

    ui.resultTable->setStyleSheet(
        "QTableWidget { "
        "   border-image: url(:/icons/sql.png);"
        "   border: none; "                               
        "   background-color: rgba(255, 255, 255, 0.85);"
        "   color: #333333; "
        "}"
        "QHeaderView { "
        "   background-color: transparent; "
        "}"
        "QHeaderView::section { "
        "   background-color: transparent; "
        "   border: none; "                 
        "   font-weight: bold; "            
        "   color: #333333; "               
        "   padding: 4px; "
        "}"
        "QHeaderView::section:horizontal { "
        "   border-bottom: 1px solid black; "
        "}"

        "QHeaderView::section:vertical { "
        "   border-right: 1px solid black; "
        "}"

        "QTableCornerButton::section { "
        "   background-color: transparent; "
        "   border: none; "
        "}"
    );

    ui.sqlEditor->setText("LOGIN admin 'admin123';");
    ui.treeWidget->setHeaderLabel("对象资源管理器");
    QTreeWidgetItem* userRoot = new QTreeWidgetItem(ui.treeWidget);
    userRoot->setText(0, "👤 ZHANG_HANWEI"); 

    //一级分类节点
    QTreeWidgetItem* tablesNode = new QTreeWidgetItem(userRoot);
    tablesNode->setText(0, "📁 表");

    QTreeWidgetItem* viewsNode = new QTreeWidgetItem(userRoot);
    viewsNode->setText(0, "📁 视图");

    QTreeWidgetItem* indexesNode = new QTreeWidgetItem(userRoot);
    indexesNode->setText(0, "📁 索引");

    QTreeWidgetItem* proceduresNode = new QTreeWidgetItem(userRoot);
    proceduresNode->setText(0, "📁 存储过程");

    // 模拟数据表：ACCOUNT
    QTreeWidgetItem* accountTable = new QTreeWidgetItem(tablesNode);
    accountTable->setText(0, "ACCOUNT");

    // 表下的二级分类：列
    QTreeWidgetItem* accCols = new QTreeWidgetItem(accountTable);
    accCols->setText(0, "列");
    (new QTreeWidgetItem(accCols))->setText(0, "ID (INT, PK)");
    (new QTreeWidgetItem(accCols))->setText(0, "USERNAME (VARCHAR)");
    (new QTreeWidgetItem(accCols))->setText(0, "BALANCE (DOUBLE)");

    // 表下的二级分类：约束
    QTreeWidgetItem* accCons = new QTreeWidgetItem(accountTable);
    accCons->setText(0, "约束 ");
    (new QTreeWidgetItem(accCons))->setText(0, "CHECK_BALANCE_POSITIVE");

    // 默认展开当前用户的“表”目录
    userRoot->setExpanded(true);
    tablesNode->setExpanded(true);

    ui.logBrowser->append("[系统] 客户端已启动。");

    // 3. 初始化右下角的查询结果表格
    ui.resultTable->clear(); // 清空可能已有的数据和表头
    ui.resultTable->setColumnCount(4); // 设置为4列
    ui.resultTable->setHorizontalHeaderLabels(QStringList() << "员工ID" << "姓名" << "部门与职位" << "入职日期");

    // 设置5行测试数据
    ui.resultTable->setRowCount(5);

    // 第1行：普通中文与数字
    ui.resultTable->setItem(0, 0, new QTableWidgetItem("1001"));
    ui.resultTable->setItem(0, 1, new QTableWidgetItem("张三"));
    ui.resultTable->setItem(0, 2, new QTableWidgetItem("研发部 后端工程师"));
    ui.resultTable->setItem(0, 3, new QTableWidgetItem("2023-07-01"));

    // 第2行：普通中文与数字
    ui.resultTable->setItem(1, 0, new QTableWidgetItem("1002"));
    ui.resultTable->setItem(1, 1, new QTableWidgetItem("李四"));
    ui.resultTable->setItem(1, 2, new QTableWidgetItem("研发部 前端工程师"));
    ui.resultTable->setItem(1, 3, new QTableWidgetItem("2023-08-15"));

    ui.resultTable->setItem(2, 0, new QTableWidgetItem("1003"));
    ui.resultTable->setItem(2, 1, new QTableWidgetItem("王五"));
    ui.resultTable->setItem(2, 2, new QTableWidgetItem("产品部经理, 兼任Scrum Master"));
    ui.resultTable->setItem(2, 3, new QTableWidgetItem("2022-01-10"));

    // 第4行：包含纯英文
    ui.resultTable->setItem(3, 0, new QTableWidgetItem("1004"));
    ui.resultTable->setItem(3, 1, new QTableWidgetItem("Alice"));
    ui.resultTable->setItem(3, 2, new QTableWidgetItem("UI/UX Designer"));
    ui.resultTable->setItem(3, 3, new QTableWidgetItem("2024-02-20"));

    // 第5行：特殊符号
    ui.resultTable->setItem(4, 0, new QTableWidgetItem("1005"));
    ui.resultTable->setItem(4, 1, new QTableWidgetItem("赵六"));
    ui.resultTable->setItem(4, 2, new QTableWidgetItem("系统运维 (DBA & DevOps)"));
    ui.resultTable->setItem(4, 3, new QTableWidgetItem("2021-11-11"));

    // 优化显示：让表格的列宽自动适应文字长度，看起来更美观
    ui.resultTable->resizeColumnsToContents();

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

    // 开启 SQL 编辑器的事件拦截（用于 Tab 补全）
    ui.sqlEditor->installEventFilter(this);

    // 绑定按钮：导出为 Excel (CSV格式)
    connect(ui.btnExport, &QPushButton::clicked, this, [=]() {
        // 检查表格是否有数据
        if (ui.resultTable->rowCount() == 0) {
            QMessageBox::warning(this, "提示", "当前没有可导出的查询结果！");
            return;
        }

        // 弹出保存文件对话框
        QString fileName = QFileDialog::getSaveFileName(this, "导出 Excel", "", "CSV 文件 (*.csv)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "错误", "无法创建或写入文件！");
            return;
        }

        QTextStream out(&file);
        // 写入 UTF-8 BOM 头，防止用 Excel 打开时中文变成乱码
        out.setGenerateByteOrderMark(true);

        // 1. 写入表头
        for (int c = 0; c < ui.resultTable->columnCount(); ++c) {
            out << ui.resultTable->horizontalHeaderItem(c)->text();
            if (c < ui.resultTable->columnCount() - 1) out << ",";
        }
        out << "\n";

        // 2. 写入数据行
        for (int r = 0; r < ui.resultTable->rowCount(); ++r) {
            for (int c = 0; c < ui.resultTable->columnCount(); ++c) {
                QTableWidgetItem* item = ui.resultTable->item(r, c);
                QString text = item ? item->text() : "";

                // 处理数据中本身包含逗号的情况（用双引号包裹）
                if (text.contains(",")) {
                    text = "\"" + text + "\"";
                }
                out << text;

                if (c < ui.resultTable->columnCount() - 1) out << ",";
            }
            out << "\n";
        }

        file.close();
        ui.logBrowser->append("<font color='cyan'>[系统] 数据已成功导出至: " + fileName + "</font>");
        QMessageBox::information(this, "成功", "导出完毕！可以直接用 Excel 打开。");
        });
}

bool DBMS_Client::eventFilter(QObject* watched, QEvent* event)
{
    // 如果是我们的 SQL 输入框，并且是键盘按下事件
    if (watched == ui.sqlEditor && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // 捕捉 Tab 键
        if (keyEvent->key() == Qt::Key_Tab) {
            QTextCursor cursor = ui.sqlEditor->textCursor();

            // 选中光标所在的当前单词
            cursor.select(QTextCursor::WordUnderCursor);
            QString currentWord = cursor.selectedText().toUpper();

            // 定义我们的补全词典
            std::map<QString, QString> keywords = {
                {"SEL", "SELECT "}, {"CRE", "CREATE "}, {"TAB", "TABLE "},
                {"DAT", "DATABASE "}, {"INS", "INSERT INTO "}, {"UPD", "UPDATE "},
                {"DEL", "DELETE FROM "}, {"WHE", "WHERE "}, {"FRO", "FROM "},
                {"VAL", "VALUES "}
            };

            // 查找匹配项进行替换
            for (const auto& pair : keywords) {
                if (pair.first.startsWith(currentWord) || currentWord == pair.first) {
                    cursor.insertText(pair.second);
                    return true; // 返回 true 表示我们已经处理了这个 Tab 按键，不要再输入制表符了
                }
            }

            // 如果没匹配到，取消选中状态，让普通的 Tab 制表符正常输入
            cursor.clearSelection();
            ui.sqlEditor->setTextCursor(cursor);
        }
    }

    // 其他事件正常交给父类处理
    return QMainWindow::eventFilter(watched, event);
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
