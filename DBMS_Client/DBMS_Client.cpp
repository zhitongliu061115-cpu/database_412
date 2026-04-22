#include "DBMS_Client.h"

DBMS_Client::DBMS_Client(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this); // 这行代码会自动把你在 .ui 里画的界面加载出来

    // 1. 给 SQL 输入框一个默认提示
    ui.sqlEditor->setText("SELECT * FROM Account;");

    // 2. 初始化左侧的数据库树 (QTreeWidget)
    ui.treeWidget->setHeaderLabel("对象资源管理器");

    // 创建一个数据库节点
    QTreeWidgetItem* dbNode = new QTreeWidgetItem(ui.treeWidget);
    dbNode->setText(0, "Ruanko_DB"); // 模拟文档里提到的系统数据库

    // 在数据库节点下创建一个表节点
    QTreeWidgetItem* tableNode = new QTreeWidgetItem(dbNode);
    tableNode->setText(0, "Account表");

    // 展开树
    ui.treeWidget->expandAll();

    // 3. 初始化右下角的查询结果表格 (QTableWidget)
    ui.resultTable->setColumnCount(3); // 设置3列
    ui.resultTable->setHorizontalHeaderLabels(QStringList() << "ID" << "用户名" << "余额"); // 设置表头

    // 塞入两行假数据
    ui.resultTable->setRowCount(2);
    ui.resultTable->setItem(0, 0, new QTableWidgetItem("1"));
    ui.resultTable->setItem(0, 1, new QTableWidgetItem("Admin"));
    ui.resultTable->setItem(0, 2, new QTableWidgetItem("9999.00"));

    ui.resultTable->setItem(1, 0, new QTableWidgetItem("2"));
    ui.resultTable->setItem(1, 1, new QTableWidgetItem("TestUser"));
    ui.resultTable->setItem(1, 2, new QTableWidgetItem("100.50"));

    // 4. 初始化日志输出
    ui.logBrowser->append("[系统] 客户端已启动...");
    ui.logBrowser->append("[系统] 等待连接到 DBMS 服务端...");
}

DBMS_Client::~DBMS_Client()
{
}