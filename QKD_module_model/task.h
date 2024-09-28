#ifndef TASK_H
#define TASK_H

#include "../include/qkdalice.h"
#include "../include/qkdbob.h"

#include <QObject>
#include <QThread>
#include <QRandomGenerator>
#include <QtConcurrent/QtConcurrent>

class Task : public QObject
{
    Q_OBJECT
public:
    struct Parameters
    {
        double DetectionEfficiency;
        double PhotonNumber;
        double TotalAttenuation;
        double LineLength;
        double RealErrorRate;
        double RequiredErrorRate;
        size_t RawKeySize;
    };

    explicit Task(QCoreApplication *parent = nullptr);

public slots:
    void run();
    void publishLogMessage(const QString& message);

signals:
    void finished();

private:
    detectorClick chooseDetectorClick(rawData data);
    void generateRandomKey();
    void makeSiftedKey();
    void checkKey();
    void strengthenKey();
    Parameters parseParams(QCoreApplication * app);


    QKDAlice *mAlice;
    QKDBob *mBob;
    QThread *thBobProcess;
    QFuture<void> BobFuture;
    Parameters parameters;
    QRandomGenerator randomGenerator;
};

#endif // TASK_H
