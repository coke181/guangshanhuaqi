#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    // Qt 应用入口只负责初始化 UI 外壳，不参与任何渲染计算。
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("软光栅化器"));
    app.setApplicationDisplayName(QStringLiteral("软光栅化器"));
    app.setOrganizationName(QStringLiteral("个人项目"));

    // 主窗口内部会创建显示控件，并驱动软光栅化器按帧刷新。
    MainWindow window;
    window.show();
    return app.exec();
}
