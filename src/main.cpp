#include <QApplication>
#include <QMainWindow>
#include "terminal/terminalwidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QMainWindow w;
    w.setWindowTitle("Qt Embedded Terminal Demo");
    w.resize(800, 600);
    
    TerminalWidget *terminal = new TerminalWidget(&w);
    w.setCentralWidget(terminal);
    
    // Start terminal shell.
    terminal->startShell("");
    
    w.show();
    return a.exec();
}
