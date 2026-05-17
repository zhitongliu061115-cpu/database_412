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
    ui.sqlEditor->setEnabled(false);
    ui.btnRun->setEnabled(false);
    ui.sqlEditor->setPlaceholderText("请在左侧对象资源管理器中，右键点击用户并打开查询控制台...");

    ui.treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.treeWidget, &QTreeWidget::customContextMenuRequested, this, &DBMS_Client::showTreeContextMenu);
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

    ui.treeWidget->clear();
    ui.treeWidget->setHeaderLabel("对象资源管理器");

    ui.logBrowser->append("[系统] 客户端已启动。");

    // 初始化右下角的查询结果表格
    ui.resultTable->clear(); // 清空可能已有的数据和表头
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
            // 【核心修改】打包会话身份与SQL内容
            QString payload = "USER:" + m_currentActiveUser + "|SQL:" + sqlText;
            tcpSocket->write(payload.toUtf8());

            ui.logBrowser->append("<font color='white'><b>[" + m_currentActiveUser + " 控制台发送]</b> " + sqlText + "</font>");
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

    connect(ui.treeWidget, &QTreeWidget::itemDoubleClicked, this, &DBMS_Client::onTreeItemDoubleClicked);
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
    ui.logBrowser->append("<font color='green'>[网络] 成功连接到 DBMS 服务端！</font>");
    ui.logBrowser->append("[系统] 正在向网络请求同步 Schema 目录结构...");

    // 【核心拉取触发点】连接成功后，立刻向服务端发送获取最新目录树的指令
    tcpSocket->write("INTERNAL_CMD:FETCH_SCHEMA");
}

void DBMS_Client::onReadyRead()
{
    // 读取服务端返回的数据
    QByteArray data = tcpSocket->readAll();
    QString response = QString::fromUtf8(data).trimmed();

    if (response.startsWith("SCHEMA_SYNC")) {
        // 1. 清空旧的树节点，准备重新绘制
        ui.treeWidget->clear();
        ui.treeWidget->setHeaderLabel("对象资源管理器");

        // 2. 建立一个映射表，用于快速根据用户名找到对应的树节点
        std::map<QString, QTreeWidgetItem*> userNodeMap;

        // 3. 将报文按照 "|" 进行切分
        QStringList tokens = response.split("|", Qt::SkipEmptyParts);

        for (const QString& token : tokens) {
            // 解析用户节点
            if (token.startsWith("USER:")) {
                QString username = token.mid(5).trimmed(); // 提取 "USER:" 后面的用户名

                // 创建一级根节点（用户模式）
                QTreeWidgetItem* userRoot = new QTreeWidgetItem(ui.treeWidget);
                userRoot->setText(0, "👤 " + username);
                userNodeMap[username] = userRoot;

                // 默认初始化 Oracle 风格的空二级目录文件夹
                QTreeWidgetItem* tablesFolder = new QTreeWidgetItem(userRoot);
                tablesFolder->setText(0, "📁 表");

                QTreeWidgetItem* viewsFolder = new QTreeWidgetItem(userRoot);
                viewsFolder->setText(0, "📁 视图");

                QTreeWidgetItem* indexesFolder = new QTreeWidgetItem(userRoot);
                indexesFolder->setText(0, "📁 索引");

                QTreeWidgetItem* procsFolder = new QTreeWidgetItem(userRoot);
                procsFolder->setText(0, "📁 存储过程");
            }
            // 解析具体的数据表文件节点
            else if (token.startsWith("TABLE:")) {
                // 报文标准格式为：TABLE:用户名:表名
                QStringList tableParts = token.split(":");
                if (tableParts.size() == 3) {
                    QString username = tableParts[1];
                    QString tablename = tableParts[2];

                    // 检查该用户节点是否存在
                    if (userNodeMap.find(username) != userNodeMap.end()) {
                        QTreeWidgetItem* userRoot = userNodeMap[username];

                        // 按照我们上面初始化的顺序，第 0 个子节点是 "表 (Tables)" 文件夹
                        QTreeWidgetItem* tablesFolder = userRoot->child(0);

                        // 将具体的表挂载到该用户的“表”文件夹内
                        QTreeWidgetItem* tableItem = new QTreeWidgetItem(tablesFolder);
                        tableItem->setText(0, "📊 " + tablename);
                    }
                }
            }
        }

        // 4. 优化视觉体验：默认展开树状结构，并输出提示
        ui.treeWidget->expandAll();
        ui.logBrowser->append("<font color='cyan'>[系统] 成功拉取服务器 data/Schema/ 文件数据，目录树同步完毕。</font>");
        return; // 解析完成，拦截结束
    }

    // 2. 智能二维表格解析器 (针对 RecordManager.cpp 的输出格式)
     // ==========================================
     // 检查是否符合后端 select 语句的输出特征：包含制表符、分割线、以及底部的统计字眼
    if (response.contains("\t") && response.contains("---") && response.contains("共")) {
        QStringList lines = response.split('\n', Qt::SkipEmptyParts);

        int separatorIndex = -1;
        // 寻找由 50 个 '-' 组成的分割线所在行
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].contains("----------")) {
                separatorIndex = i;
                break;
            }
        }

        // 如果找到了分割线，并且分割线上面至少有一行（表头）
        if (separatorIndex > 0) {
            ui.resultTable->clear();
            ui.resultTable->setRowCount(0);

            // 1. 解析表头 (分割线的上一行)，后端用的是 '\t' 分隔
            QString headerLine = lines[separatorIndex - 1].trimmed();
            QStringList headers = headerLine.split('\t', Qt::SkipEmptyParts);

            ui.resultTable->setColumnCount(headers.size());
            ui.resultTable->setHorizontalHeaderLabels(headers);

            // 2. 解析数据 (从分割线的下一行开始，直到遇到 "共 x 条记录")
            int rowCount = 0;
            for (int r = separatorIndex + 1; r < lines.size(); ++r) {
                QString currentLine = lines[r].trimmed();

                // 遇到结尾的统计行，直接结束解析
                if (currentLine.startsWith("共")) {
                    break;
                }

                // 插入新的一行
                ui.resultTable->insertRow(rowCount);

                // 后端数据是以 '\t' 分隔的。这里不要用 SkipEmptyParts，防止某列为空导致数据错位
                QStringList columns = currentLine.split('\t');
                for (int c = 0; c < columns.size() && c < headers.size(); ++c) {
                    QTableWidgetItem* item = new QTableWidgetItem(columns[c].trimmed());
                    item->setTextAlignment(Qt::AlignCenter);
                    ui.resultTable->setItem(rowCount, c, item);
                }
                rowCount++;
            }

            // 自动调整列宽
            ui.resultTable->resizeColumnsToContents();
            ui.logBrowser->append("<font color='cyan'>[系统] 发现 SELECT 结果集，已成功渲染至可视化表格。</font>");
            return; // 解析完毕，直接返回，不再把脏数据打印到底部日志框中
        }
    }

    // 如果不是系统内部同步指令，则走普通的 SQL 结果打印逻辑
    ui.logBrowser->append(response);
}

void DBMS_Client::onError(QAbstractSocket::SocketError socketError)
{
    ui.logBrowser->append("<font color='red'>[错误] 网络连接异常: " + tcpSocket->errorString() + "</font>");
}

void DBMS_Client::showTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = ui.treeWidget->itemAt(pos);
    QMenu menu(this);

    QAction* actOpenConsole = nullptr;
    QAction* actAddUser = nullptr;
    QString username = "";

    // 1. 如果点击的是根级用户节点
    if (item && item->parent() == nullptr) {
        // 提取干净的用户名
        username = item->text(0).replace("👤", "").replace("(User)", "").trimmed();
        actOpenConsole = menu.addAction("▶️ 新建查询控制台 (" + username + ")");
        menu.addSeparator(); // 加一条优雅的分割线
    }
    // 2. 如果点击的是空白区域
    else if (!item) {
        actAddUser = menu.addAction("➕ 注册新用户");
        menu.addSeparator();
    }

    // 3. 【核心新增】：无论点在哪里，全局始终提供“刷新”选项
    QAction* actRefresh = menu.addAction("🔄 刷新对象资源管理器");

    // 弹出菜单并阻塞等待用户选择
    QAction* selectedAct = menu.exec(ui.treeWidget->mapToGlobal(pos));

    if (!selectedAct) {
        return;
    }

    // 根据用户的选择执行相应操作
    if (selectedAct == actOpenConsole && !username.isEmpty()) {
        handleOpenConsole(username);
    }
    else if (selectedAct == actAddUser) {
        handleRegisterUser();
    }
    else if (selectedAct == actRefresh) {
        // 执行刷新指令
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            ui.logBrowser->append("[系统] 正在向服务端请求最新目录结构...");
            tcpSocket->write("INTERNAL_CMD:FETCH_SCHEMA");
        }
        else {
            QMessageBox::warning(this, "提示", "尚未连接到服务端，无法刷新！");
        }
    }
}

void DBMS_Client::handleRegisterUser()
{
    if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, "错误", "尚未连接到服务端，无法创建新用户！");
        return; // 直接退出，终止后续的弹窗和 UI 绘制
    }
    bool ok;
    QString username = QInputDialog::getText(this, "注册新用户", "请输入新用户名/模式名 (Schema Name):", QLineEdit::Normal, "", &ok);
    if (!ok || username.trimmed().isEmpty()) return;

    username = username.trimmed().toUpper(); // Oracle习惯大写

    // 1. 在目录树中创建对应的结构
    QTreeWidgetItem* userNode = new QTreeWidgetItem(ui.treeWidget);
    userNode->setText(0, "👤 " + username);

    // 默认创建空文件夹文件夹
    (new QTreeWidgetItem(userNode))->setText(0, "📁 表");
    (new QTreeWidgetItem(userNode))->setText(0, "📁 视图");
    (new QTreeWidgetItem(userNode))->setText(0, "📁 索引");
    (new QTreeWidgetItem(userNode))->setText(0, "📁 存储过程");

    ui.treeWidget->expandItem(userNode);
    ui.logBrowser->append("[系统] 成功在本地注册新用户模式: " + username);

    // 2. 同步通知后端：让后端在物理层面建立对应的存储环境或记录到用户管理表中
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        // 这里可以发送特殊的内部指令给后端，如 "INTERNAL_CMD:CREATE_USER:" + username
        tcpSocket->write(("INTERNAL_CMD:CREATE_USER:" + username).toUtf8());
    }
}

void DBMS_Client::handleOpenConsole(const QString& username)
{
    m_currentActiveUser = username;

    // 解锁编辑器与执行按钮
    ui.sqlEditor->setEnabled(true);
    ui.btnRun->setEnabled(true);
    ui.sqlEditor->clear();
    ui.sqlEditor->setPlaceholderText("当前工作控制台已绑定用户: " + username + "，请输入 SQL 语句...");

    // 更改窗口标题，给予明确的视觉反馈
    this->setWindowTitle("412 DBMS (Client) - [当前会话: " + username + "]");
    ui.logBrowser->append("<font color='cyan'>[会话] 已成功切换并打开用户 " + username + " 的专属查询控制台。</font>");
}

void DBMS_Client::onTreeItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    if (!item) return;

    QString text = item->text(0);
    // 判断双击的是不是数据表节点 (带有 📊 图标)
    if (text.startsWith("📊")) {
        QString tableName = text.replace("📊", "").trimmed();

        // 向上寻找父节点，直到找到根级的用户节点
        QTreeWidgetItem* parent = item->parent();
        if (parent && parent->parent() != nullptr) {
            QString username = parent->parent()->text(0).replace("👤", "").trimmed();

            // 1. 自动切换到该用户的查询控制台
            handleOpenConsole(username);

            // 2. 自动在输入框写入全表查询语句
            QString sql = "SELECT * FROM " + tableName + ";";
            ui.sqlEditor->setPlainText(sql);

            // 3. 模拟用户点击了“执行”按钮
            ui.btnRun->click();

            ui.logBrowser->append("<font color='cyan'>[系统] 正在请求表 " + tableName + " 的数据...</font>");
        }
    }
}
