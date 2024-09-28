#include "task.h"

#include <QStringList>

Task::Task(QCoreApplication *parent)
    : QObject(parent),
      mAlice(CreateAliceInterface()),
      mBob(CreateBobInterface())
{
    mBob->setTCPIPaddress("127.0.0.1");
    mBob->setPort(60000);

    mAlice->setTCPIPaddress("127.0.0.1");
    mAlice->setPort(60000);

    // запускаем сервер Боба в отдельном потоке
    BobFuture = QtConcurrent::run(mBob,&QKDBob::StartBobProcess);

    // соединяем Сокет Алисы с сервером Боба

    mAlice->connectToServer();

    connect(mBob, &QKDBob::logMessageGenerated, this, &Task::publishLogMessage);

    connect(mAlice, &QKDAlice::logMessageGenerated, this, &Task::publishLogMessage);

    mAlice->clearSiftedKey();
    mBob->clearSiftedKey();

    randomGenerator.seed((QTime::currentTime().msecsSinceStartOfDay()));

    parameters = parseParams(parent);
}


void Task::run()
{
    generateRandomKey();
    makeSiftedKey();
    strengthenKey();
    checkKey();

    mBob->StopBobProcess();

    emit finished();
}


void Task::generateRandomKey()
{   
    int Count = parameters.RawKeySize;
    mAlice->setMaxSize(Count);
    mBob->setMaxSize(Count);
    mAlice->mRawKey.clear();
    mBob->mRawKey.clear();
    mBob->preSiftedKey.clear();
    mAlice->preSiftedKey.clear();
    mBob->clearSiftedKey();
    mAlice->clearSiftedKey();
    mBob->mDetectorClicks.clear();
    double error = parameters.RealErrorRate/100.0;

    mBob->errorRate = error;
    mAlice->errorRate = error;

    double detectionProbablity = qPow(10, -0.1 * parameters.TotalAttenuation * parameters.LineLength);

    mAlice->setTotalAttenuation(detectionProbablity);
    mAlice->setPhotonNumber(parameters.PhotonNumber);
    mAlice->setDetectionEfficiency(parameters.DetectionEfficiency/100.0);
    mAlice->setRequiredErrorRate(1e-9);

    detectionProbablity = detectionProbablity * parameters.DetectionEfficiency/100.0 * parameters.PhotonNumber;

    qInfo() << "Генерация ключа...";

    for (int i = 0; i<Count; ++i) {
        rawData tempdata;
        tempdata.pos = i;
        quint8 temp = ((quint8)randomGenerator()) & 0x03;
        tempdata.Bit = temp & 0x01;
        tempdata.Base = (temp>>1) & 0x01;

        // проверка вероятности детектирования сигнала
        // метод Монте-Карло
        double probablity = randomGenerator.generateDouble();
        if (probablity<=detectionProbablity)
        {
            mAlice->mRawKey.append(tempdata);

            // добавление случайной ошибки с вероятностью error
            probablity = randomGenerator.generateDouble();
            if (probablity<error)
                tempdata.Bit = 1-tempdata.Bit;
            detectorClick click = chooseDetectorClick(tempdata);
            click.pos = i;
            mBob->mDetectorClicks.append(click);
        }
    }
    qInfo() << "Сырой ключ сгенерирован";
}


void Task::makeSiftedKey()
{
    // Шаг 1. Получение-передача Бобом номеров и базисов данных
    mAlice->rawData2preSiftedKey();
    qInfo() << "Размер предпросеянного ключа Алисы: " << mAlice->preSiftedKey.size() << "Боба: " << mBob->preSiftedKey.size();
    // Шаг 2. Собственно сам процесс расчета ошибки и усиления ключа
    mAlice->siftedKeyProcess();
}


void Task::checkKey()
{
    int temp1 = mAlice->getSiftedKeySize();
    int temp2 = mBob->getSiftedKeySize();
    if (temp1!=temp2) {
        qInfo() << "Размеры ключей Алисы и Боба не одинаковы!";
        return;
    }
    QByteArray ba1;
    mAlice->getSiftedKey(temp1, &ba1);
    QByteArray ba2;
    mBob->getSiftedKey(temp1, &ba2);
    for (int i = 0; i < temp1; ++i) {
        if (ba1[i] != ba2[i]) {
            qInfo() << "Ключи Алисы и Боба отличаются в байте №" << i;
            return;
        }
    }

    qInfo() << "Ключи идентичны";
}


void Task::strengthenKey()
{
    mAlice->strengthenedKeyProcess();
}


detectorClick Task::chooseDetectorClick(rawData data)
{
    float detectionProbablity = (float)qrand()/(float)RAND_MAX;
    detectorClick click;

    switch (data.Base)
    {
        case 0:
        {
            if (detectionProbablity < 0.5)
            {
                click.detectorNumber = 0;
            }
            else
            {
                click.detectorNumber = 1;
            }

            if (detectionProbablity < 0.25 || (detectionProbablity >= 0.5 && detectionProbablity < 0.75))
            {
                click.intervalNumber = data.Bit == 0 ? 0 : 2;
            }
            else
            {
                click.intervalNumber = 1;
            }
            break;
        }
        case 1:
        {
            if (detectionProbablity < 0.75)
            {
                click.detectorNumber = data.Bit == 0 ? 0 : 1;
            }
            else
            {
                click.detectorNumber = data.Bit == 0 ? 1 : 0;
            }

            if (detectionProbablity >= 0.125 && detectionProbablity < 0.625)
            {
                click.intervalNumber = 1;
            }
            else if (detectionProbablity < 0.125 || (detectionProbablity >= 0.75 && detectionProbablity < 0.875))
            {
                click.intervalNumber = 0;
            }
            else if ((detectionProbablity >= 0.625 && detectionProbablity < 0.75) || (detectionProbablity >= 0.875))
            {
                click.intervalNumber = 2;
            }
            break;
        }
    }

    return click;
}


void Task::publishLogMessage(const QString& message)
{
    qInfo() << message;
}


Task::Parameters Task::parseParams(QCoreApplication * app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("QKD Module");
    parser.addHelpOption();

    QCommandLineOption detectionEfficiencyOption("de", "Detection efficiency, %.", "detectionEfficiency");
    parser.addOption(detectionEfficiencyOption);

    QCommandLineOption attenuationOption("att", "Attenuation, dB/km.", "attenuationOption");
    parser.addOption(attenuationOption);

    QCommandLineOption photonNumberOption("pn", "Photon number.", "photonNumberOption");
    parser.addOption(photonNumberOption);

    QCommandLineOption errorRateOption("er", "Error rate, %.", "errorRateOption");
    parser.addOption(errorRateOption);

    QCommandLineOption lineLengthOption("ll", "Line length, km.", "lineLengthOption");
    parser.addOption(lineLengthOption);

    QCommandLineOption rawKeyLengthOption("kl", "Raw key length, bit.", "lineLengthOption");
    parser.addOption(rawKeyLengthOption);

    // Process the actual command line arguments given by the user
    parser.process(*app);

    Parameters params;

    params.DetectionEfficiency = parser.value(detectionEfficiencyOption).toDouble();
    params.TotalAttenuation = parser.value(attenuationOption).toDouble();
    params.PhotonNumber = parser.value(photonNumberOption).toDouble();
    params.RealErrorRate = parser.value(errorRateOption).toDouble();
    params.LineLength = parser.value(lineLengthOption).toDouble();
    params.RawKeySize = parser.value(rawKeyLengthOption).toULong();

    return params;
}
