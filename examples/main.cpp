#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QProcessEnvironment>
#include <QFile>
#include "terminal/terminalwidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QMainWindow w;
    w.setWindowTitle("Qt Embedded Multi-Terminal Demo");
    w.resize(1000, 700); // 适当调大尺寸以适应多 Tab 结构
    
    // 主容器和布局
    QWidget *centralWidget = new QWidget(&w);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);
    
    // 顶部控制条布局
    QHBoxLayout *controlLayout = new QHBoxLayout();
    QLabel *label = new QLabel("终端类型: ", centralWidget);
    QComboBox *shellCombo = new QComboBox(centralWidget);
    QPushButton *btnNew = new QPushButton("新建终端 (+)", centralWidget);
    
    // 添加 Shell 选项和底层运行路径数据 (利用 itemData 储存实际 path)
    shellCombo->addItem("默认系统 Shell (COMSPEC)", "");
    shellCombo->addItem("Windows PowerShell (powershell.exe)", "powershell.exe");
    shellCombo->addItem("PowerShell Core (pwsh.exe)", "pwsh.exe");
    shellCombo->addItem("Command Prompt (cmd.exe)", "cmd.exe");
    
    // 防御性探测 Git Bash 路径并添加
    QString gitBashPath = "C:\\Program Files\\Git\\bin\\bash.exe";
    if (!QFile::exists(gitBashPath)) {
        QString userProfile = QProcessEnvironment::systemEnvironment().value("USERPROFILE");
        gitBashPath = userProfile + "\\AppData\\Local\\Programs\\Git\\bin\\bash.exe";
    }
    shellCombo->addItem("Git Bash (bash.exe)", gitBashPath);
    
    controlLayout->addWidget(label);
    controlLayout->addWidget(shellCombo);
    controlLayout->addWidget(btnNew);
    controlLayout->addStretch();
    
    mainLayout->addLayout(controlLayout);
    
    // 核心 Tab 容器
    QTabWidget *tabWidget = new QTabWidget(centralWidget);
    tabWidget->setTabsClosable(true);    // 支持点击关闭页签
    tabWidget->setDocumentMode(true);    // 使用精美的浏览器/编辑器无边框扁平样式
    mainLayout->addWidget(tabWidget, 1); // 占据全部剩余拉伸空间
    
    w.setCentralWidget(centralWidget);
    
    // 帮助 lambda 函数：新建一个 Tab 终端
    auto fnCreateNewTab = [tabWidget, shellCombo](const QString &targetName = "", const QString &targetPath = "default") {
        QString shellName = targetName;
        QString shellPath = targetPath;
        
        if (shellPath == "default") {
            shellName = shellCombo->currentText();
            shellPath = shellCombo->currentData().toString();
        }
        
        // 实例化新终端控件并加入 Tab
        TerminalWidget *terminal = new TerminalWidget(tabWidget);
        int index = tabWidget->addTab(terminal, shellName);
        
        // 启动该终端并自动定位至新 Tab
        terminal->startShell(shellPath);
        tabWidget->setCurrentIndex(index);
        terminal->setFocus(); // 🌟 自动归还键盘输入焦点，体验丝滑
    };
    
    // 信号槽：点击新建按钮时触发
    QObject::connect(btnNew, &QPushButton::clicked, [fnCreateNewTab]() {
        fnCreateNewTab();
    });
    
    // 信号槽：点击 Tab 关闭按钮时触发
    QObject::connect(tabWidget, &QTabWidget::tabCloseRequested, [tabWidget](int index) {
        QWidget *w = tabWidget->widget(index);
        if (w) {
            tabWidget->removeTab(index);
            w->deleteLater(); // 🌟 安全异步销毁 TerminalWidget，由于我们修复了析构死锁，这里将平滑释放资源并释放 PTY
        }
    });
    
    // 🌟 信号槽：在 Tab 切换时自动让当前激活的页签终端重新夺回键盘焦点，免去鼠标多余点击
    QObject::connect(tabWidget, &QTabWidget::currentChanged, [tabWidget](int index) {
        if (index >= 0) {
            TerminalWidget *terminal = qobject_cast<TerminalWidget*>(tabWidget->widget(index));
            if (terminal) {
                terminal->setFocus();
            }
        }
    });
    
    // 默认初始创建一个默认系统 Shell 页签，不留空白
    fnCreateNewTab("默认系统 Shell (COMSPEC)", "");
    
    w.show();
    return a.exec();
}
