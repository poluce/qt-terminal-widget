#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QProcessEnvironment>
#include <QFile>
#include "terminal/terminalwidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QMainWindow w;
    w.setWindowTitle("Qt Embedded Terminal Demo");
    w.resize(800, 600);
    
    // 主容器和布局
    QWidget *centralWidget = new QWidget(&w);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);
    
    // 顶部控制条布局
    QHBoxLayout *controlLayout = new QHBoxLayout();
    QLabel *label = new QLabel("选择使用 Shell: ", centralWidget);
    QComboBox *shellCombo = new QComboBox(centralWidget);
    
    // 添加 Shell 选项和底层运行路径数据 (利用 itemData 储存实际 path)
    shellCombo->addItem("默认系统 Shell (COMSPEC)", "");
    shellCombo->addItem("Windows PowerShell", "powershell.exe");
    shellCombo->addItem("Command Prompt (cmd.exe)", "cmd.exe");
    
    // 防御性探测 Git Bash 路径并添加
    QString gitBashPath = "C:\\Program Files\\Git\\bin\\bash.exe";
    if (!QFile::exists(gitBashPath)) {
        // 尝试查找 AppData 下的本地安装路径
        QString userProfile = QProcessEnvironment::systemEnvironment().value("USERPROFILE");
        gitBashPath = userProfile + "\\AppData\\Local\\Programs\\Git\\bin\\bash.exe";
    }
    shellCombo->addItem("Git Bash (bash.exe)", gitBashPath);
    
    controlLayout->addWidget(label);
    controlLayout->addWidget(shellCombo);
    controlLayout->addStretch();
    
    mainLayout->addLayout(controlLayout);
    
    // 终端控件
    TerminalWidget *terminal = new TerminalWidget(centralWidget);
    mainLayout->addWidget(terminal, 1); // 占据剩余全部拉伸空间
    
    w.setCentralWidget(centralWidget);
    
    // 信号槽连接：实时切换并重新加载 Shell
    QObject::connect(shellCombo, &QComboBox::currentIndexChanged, [terminal, shellCombo](int index) {
        QString shellPath = shellCombo->itemData(index).toString();
        terminal->stopShell();
        terminal->startShell(shellPath);
        terminal->setFocus(); // 🌟 重新把焦点还回给终端文本框，免去用户手动点击，体验极佳
    });
    
    // 初始启动默认 Shell
    terminal->startShell("");
    
    w.show();
    return a.exec();
}
