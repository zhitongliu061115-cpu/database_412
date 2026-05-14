# 安全管理 SQL 语句清单

## 1. 说明

本文档整理当前项目里和“安全管理”相关的全部语句，包括：

- 已实现的安全管理语句
- 受权限控制的业务语句
- 演示与测试用语句

说明：

- 当前安全管理基于“当前登录用户 + 当前数据库 + 表级对象权限”
- `GRANT` 和 `REVOKE` 依赖先执行 `USE <db>`

## 2. 默认管理员

系统首次启动会自动初始化默认管理员：

- 用户名：`admin`
- 密码：`admin123`

登录语句：

```sql
LOGIN admin 'admin123';
```

## 3. 已实现的安全管理语句

### 3.1 登录

```sql
LOGIN admin 'admin123';
LOGIN alice '123456';
```

格式：

```sql
LOGIN <username> <password>;
```

### 3.2 退出登录

```sql
LOGOUT;
```

### 3.3 创建用户

```sql
CREATE USER alice IDENTIFIED BY '123456';
CREATE USER bob IDENTIFIED BY '654321';
```

格式：

```sql
CREATE USER <username> IDENTIFIED BY <password>;
```

说明：

- 仅管理员可执行

### 3.4 删除用户

```sql
DROP USER alice;
DROP USER bob;
```

格式：

```sql
DROP USER <username>;
```

说明：

- 仅管理员可执行
- 默认管理员 `admin` 不允许删除

### 3.5 授权

```sql
GRANT SELECT ON student TO alice;
GRANT INSERT ON student TO alice;
GRANT SELECT, INSERT ON student TO alice;
GRANT UPDATE, DELETE ON student TO bob;
GRANT ALTER ON student TO bob;
GRANT DROP ON student TO bob;
GRANT ALL ON student TO alice;
```

格式：

```sql
GRANT <privilege_list> ON <table_name> TO <username>;
```

支持的权限名：

- `SELECT`
- `INSERT`
- `UPDATE`
- `DELETE`
- `ALTER`
- `DROP`
- `ALL`

说明：

- 仅管理员可执行
- 需要先 `USE <db>`

### 3.6 收回权限

```sql
REVOKE SELECT ON student FROM alice;
REVOKE INSERT ON student FROM alice;
REVOKE SELECT, INSERT ON student FROM alice;
REVOKE ALL ON student FROM alice;
```

格式：

```sql
REVOKE <privilege_list> ON <table_name> FROM <username>;
```

说明：

- 仅管理员可执行
- 需要先 `USE <db>`

### 3.7 查看用户列表

```sql
SHOW USERS;
```

说明：

- 仅管理员可执行

### 3.8 查看某个用户的授权

```sql
SHOW GRANTS FOR alice;
SHOW GRANTS FOR bob;
```

格式：

```sql
SHOW GRANTS FOR <username>;
```

说明：

- 管理员可以查看所有用户
- 普通用户只能查看自己

## 4. 当前受权限控制的业务语句

下面这些不是“安全管理命令”，但已经被安全模块接管。

### 4.1 数据库级管理员操作

仅管理员可执行：

```sql
CREATE DATABASE school;
DROP DATABASE school;
CREATE TABLE student;
```

常见完整流程：

```sql
LOGIN admin 'admin123';
CREATE DATABASE school;
USE school;
CREATE TABLE student;
```

### 4.2 ALTER TABLE 需要 `ALTER` 权限

```sql
ALTER TABLE student ADD id INT NOT NULL;
ALTER TABLE student ADD name VARCHAR(20);
ALTER TABLE student DROP COLUMN name;
ALTER TABLE student MODIFY COLUMN name nickname;
```

### 4.3 SELECT 需要 `SELECT` 权限

```sql
SELECT * FROM student;
SELECT id, name FROM student;
SELECT * FROM student WHERE id = 1;
SELECT * FROM student ORDER BY id DESC;
```

### 4.4 INSERT 需要 `INSERT` 权限

```sql
INSERT INTO student VALUES (1, 'Tom');
INSERT INTO student VALUES (2, 'Jerry');
INSERT INTO student (id, name) VALUES (3, 'Alice');
```

### 4.5 UPDATE 需要 `UPDATE` 权限

```sql
UPDATE student SET name = 'Jack' WHERE id = 1;
UPDATE student SET name = 'Rose' WHERE row = 0;
```

### 4.6 DELETE 需要 `DELETE` 权限

```sql
DELETE FROM student WHERE id = 1;
DELETE FROM student WHERE row = 0;
DELETE FROM student;
```

### 4.7 DROP TABLE 需要 `DROP` 权限

```sql
DROP TABLE student;
```

## 5. 推荐演示脚本

### 5.1 管理员创建用户并授权

```sql
LOGIN admin 'admin123';
CREATE DATABASE secdb;
USE secdb;
CREATE TABLE student;
ALTER TABLE student ADD id INT NOT NULL;
ALTER TABLE student ADD name VARCHAR(20);
CREATE USER alice IDENTIFIED BY '123456';
GRANT SELECT, INSERT ON student TO alice;
SHOW USERS;
SHOW GRANTS FOR alice;
LOGOUT;
```

### 5.2 普通用户执行允许的操作

```sql
LOGIN alice '123456';
USE secdb;
INSERT INTO student VALUES (1, 'Tom');
SELECT * FROM student;
LOGOUT;
```

### 5.3 普通用户执行未授权操作

```sql
LOGIN alice '123456';
USE secdb;
DELETE FROM student WHERE id = 1;
DROP TABLE student;
ALTER TABLE student ADD age INT;
LOGOUT;
```

预期：

- 都应提示权限不足

### 5.4 管理员回收权限

```sql
LOGIN admin 'admin123';
USE secdb;
REVOKE INSERT ON student FROM alice;
SHOW GRANTS FOR alice;
LOGOUT;
```

### 5.5 回收后验证

```sql
LOGIN alice '123456';
USE secdb;
INSERT INTO student VALUES (2, 'Jerry');
SELECT * FROM student;
LOGOUT;
```

预期：

- `INSERT` 失败
- `SELECT` 仍成功

## 6. 权限与语句对应关系

| 权限 | 受控语句 |
|---|---|
| `SELECT` | `SELECT ... FROM <table>` |
| `INSERT` | `INSERT INTO <table> ...` |
| `UPDATE` | `UPDATE <table> SET ... WHERE ...` |
| `DELETE` | `DELETE FROM <table> ...` |
| `ALTER` | `ALTER TABLE <table> ...` |
| `DROP` | `DROP TABLE <table>` |
| `ALL` | 上述全部 |

## 7. 当前不支持的安全语句

下面这些语句目前未实现：

```sql
ALTER USER alice IDENTIFIED BY 'newpass';
CREATE ROLE reader;
GRANT SELECT ON student TO ROLE reader;
GRANT reader TO alice;
```

也不支持：

- 列级授权
- 视图授权
- 索引授权
- 审计日志查询语句

## 8. 使用注意

1. 普通用户在执行表级语句前，必须先登录。
2. 执行 `GRANT` / `REVOKE` 前，必须先 `USE <db>`。
3. 当前授权范围是“当前数据库下的单表”。
4. `SHOW GRANTS FOR <user>` 建议先用管理员账号测试。
