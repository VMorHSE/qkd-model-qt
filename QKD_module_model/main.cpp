#include "Task.h"

#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    setlocale(0, "");

    QCoreApplication a(argc, argv);

    // Task parented to the application so that it
        // will be deleted by the application.
    Task *task = new Task(&a);

    // This will cause the application to exit when
    // the task signals finished.
    QObject::connect(task, SIGNAL(finished()), &a, SLOT(quit()));

    // This will run the task from the application event loop.
    QTimer::singleShot(0, task, SLOT(run()));

    return a.exec();
}
