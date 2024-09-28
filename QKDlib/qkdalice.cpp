#include "qkdalice.h"
#include "QtWidgets/qapplication.h"
#include "QtGui/qguiapplication.h"
#include "qcoreapplication.h"
#include "qmath.h"
#include "LDPC.h"
#include "GaloisComputer.hpp"
#include <QTime>
#include <QDataStream>
#include <thread>

QKDAlice::QKDAlice():
    QKDUnit(),
    mSocket(new QTcpSocket())
{
    sharedMemory.setKey("AliceSecretKey");
    if (sharedMemory.isAttached()) sharedMemory.detach();
    if (!sharedMemory.create(1024*1024)){
        qDebug()<<"Unable to create shared memory segment. Error:"<<sharedMemory.errorString();
//        emit logMessageGenerated(QString("Unable to create shared memory segment. Error: %1\n").arg(sharedMemory.errorString()));
        return;
    }
}

void QKDAlice::connectToServer()
{
    mSocket->connectToHost(TCPIPaddress(), port());
    mSocket->waitForConnected();
    if (mSocket->state() != QTcpSocket::ConnectedState)
    {
        qDebug() << "Alice is not connected! Reason: " << mSocket->errorString();
    }
}

bool QKDAlice::isConnected(){return mSocket->state() == QTcpSocket::ConnectedState;}

void QKDAlice::rawData2preSiftedKey()
{
    if (!isConnected()) return;

    if (!mSocket->isOpen())
        if (!mSocket->open(QIODevice::ReadWrite)) return;

    // Запрос индексов и базисов
    QByteArray tempBa;
    tempBa.append((char)RequestRecievedIndexes);
    mSocket->write(tempBa);

    while (mSocket->bytesAvailable()<sizeof(quint32) + sizeof(quint8))
        if (!mSocket->waitForReadyRead(10000)) break;

    tempBa.clear();

    tempBa.append(mSocket->read(sizeof(quint32) + sizeof(quint8)));
    if ((int)(sizeof(quint32) + sizeof(quint8))>tempBa.size()) {
        return;
    }

    QDataStream in(&tempBa, QIODevice::ReadOnly);
    quint8 command;
    in >> command;
    quint32 size;
    in >> size;

    // Алиса проверяет, на каких индексах щёлкнул детектор, удаляет остальные
    readRecievedIndexes(size, mSocket);

    // Пересылаем обратно Бобу уже скорректированные данные по Индексам и базисам.
    writeIndexesAndBasis(SetIndexesAndBasis, mSocket);

    tempBa.clear();
    // Запрашиваем, на каких индексах детектор Боба щёлкнул верно (без неопределённости)
    tempBa.append((char)RequestRightClickedIndexes);
    mSocket->write(tempBa);

    while (mSocket->bytesAvailable()<sizeof(quint32) + sizeof(quint8))
        if (!mSocket->waitForReadyRead(10000)) break;

    tempBa.clear();

    tempBa.append(mSocket->read(sizeof(quint32) + sizeof(quint8)));
    if ((int)(sizeof(quint32) + sizeof(quint8))>tempBa.size()) {
        return;
    }

    QDataStream in1(&tempBa, QIODevice::ReadOnly);
    in1 >> command;
    in1 >> size;

    // Можем использовать ту же функцию, что и для принятых индексов, потому что смысл тот же
    readRecievedIndexes(size, mSocket);

    // очищаем Raw Data Алисы и готовим для следующей пересылки случайного ключа
    mRawKey.clear();
    // очищаем Raw data Боба
    tempBa.clear();
    tempBa.append((char)QKDUnit::ClearRawData);
    mSocket->write(tempBa);
}

// Усиление ключа
void QKDAlice::siftedKeyProcess()
{
    ldpcCorrectionProc();
    /*
    // error estimation process
    preSiftedKeySize = preSiftedKey.size();
    quint8 nextPhase;

    QTime time;
    time.start();
    while (true){
        nextPhase = errorEstimationProc();
        if (nextPhase == SendParities) break;
        if (nextPhase == ErrorAtProcess) {
            // случилась ошибка, вычищаем preSiftedKeyData
            preSiftedKey.clear();
            return;
        }
    }
//    qDebug()<<"errorEstimationProc():"<<time.restart();
    emit logMessageGenerated(QString("errorEstimationProc(): %1\n").arg(time.restart()));

    // проверяем возможность усиления ключа
    // невозможно усилить ключ
    QByteArray ba;
    if (MaxPossibleSecretKeySize()<=0){
        // посылаем Бобу сообщение об ошибке и чистим PreSifted data
        ba.clear();
        ba.append((char) ErrorAtProcess);
        mSocket->write(ba);
        preSiftedKey.clear();
        return;
    }

    // Проходим процедуру рассылки четностей и устранения ошибок в ключе
//    qDebug()<<"AliceErrorEstimation"<<AliceErrorEstimation;
    emit logMessageGenerated(QString("AliceErrorEstimation %1\n").arg(AliceErrorEstimation));
    time.start();
    while (sendParitiesProc() == SendParities);
//    qDebug()<<"SendParities():"<<time.elapsed();
    emit logMessageGenerated(QString("SendParities(): %1\n").arg(time.elapsed()));

    // privacy amplification

    // Обновляем данные ключа
    updateSiftedKey();
    // и посылаем соответствующую команду Бобу
    ba.clear();
    ba.append((quint8)FinishProcess);
    mSocket->write(ba);
    */
}

void QKDAlice::setSystemParams(const SystemParams Params){mParams = Params;}

void QKDAlice::setTotalAttenuation(const float value){mParams.TotalAttenuation = value;}

void QKDAlice::setDetectionEfficiency(const float value){mParams.DetectionEfficiency = value;}

void QKDAlice::setPhotonNumber(const float value){mParams.PhotonNumber = value;}

void QKDAlice::setRequiredErrorRate(const float value){mParams.RequiredErrorRate = value;}

SystemParams QKDAlice::getSystemParams(){return mParams;}

quint32 myRandomGenerator(){return qrand()%2;}

quint32 nextRandomBit (quint32 startBit, quint32 dataSize){
    quint32 bitPosition = startBit;
    // выбираем случайное положение бита
    bitPosition += (quint32)(dataSize/2.0)*myRandomGenerator();
    bitPosition += 16*myRandomGenerator();
    bitPosition += 8*myRandomGenerator();
    bitPosition += 4*myRandomGenerator();
    bitPosition += 2*myRandomGenerator();
    bitPosition += myRandomGenerator();
    bitPosition = bitPosition % dataSize;
    return bitPosition;
}


quint8 QKDAlice::errorEstimationProc()
{
    float deltaError;
    deltaError = AliceErrorEstimation = 1000.0;

    // массив данных для работы с данными Боба
    QByteArray ba;
    QDataStream dataIn(&ba, QIODevice::ReadOnly);
    QDataStream dataOut(&ba, QIODevice::WriteOnly);

    QVector<quint32> dataSubset;

    // первый проход процедуры
    if (preSiftedKey.size() == preSiftedKeySize) errorCount = 0;

    // уже не первый проход
    // производим оценку вероятности ошибки
    else {
        // пытаемся принять данные
        // принимаем число ошибок в переданной ранее последовательности
        // обновляем errorCount

        while (mSocket->bytesAvailable()<(sizeof(quint32)+sizeof(quint8)))
            if (!mSocket->waitForReadyRead(10000)) return ErrorAtProcess;

        ba.clear();
        ba.append(mSocket->read(mSocket->bytesAvailable()));
        quint8 cmd;
        dataIn >> cmd;
        if (cmd != ErrorEstimation) return ErrorAtProcess;
        quint32 errors;
        dataIn >> errors;
        errorCount += errors;

        // расчитываем ошибку

        /** Оценка ошибки делется из следующих соображений:
         *
         * Основные обозначения:
         * N - число попыток (в нашем случае величина открытого ключа)
         * n - число удачных событий (в нашем случае число появившихся ошибок)
         * p - истинная вероятность удачного события
         * q - истинная вероятность неудачного события (q = 1-p)
         * f - экспериментальная частота удачных событий (f = n/N)
         * p1 - нижняя граница доверительного интервала
         * p2 - верхняя граница доверительного интервала
         * a - вероятность того, что истинная вероятность p лежит вне диапазона p1-p2
         *
         * Предполагается, что появление ошибки носит Пуассоновский характер, т.е. мы имеем дело
         * с пуассоновским распределением, которое при больших N  переходит в нормальное распределение
         * со средним = Np и дисперсией Npq.
         *
         * В качестве критерия выбора доверительного интервала берем 3 сигма. В этом случае вероятность a = 0.272% = 2.72е-3
         * В будущем можно выбрать более сильный критерий
         * Например
         * при  3.5 сигма a =   4.8e-4
         *      4               6e-5
         *      4.5             <1e-5
         *
         * сигма = sqrt(Npq)=sqrt(n*(N-n)/N)
         *
         * В качестве критерия выхода ставим чтобы наш доверительный интервал был на порядок уже чем
         * величина определенной ошибки.
         *
         * В общем случае нам нужно решить 2 квадратных уравнения, определяющих верхнюю и нижние границы
         * Мы посчитаем, что верхняя и нижняя граница доверительного интервала находятся
         * на одинаковом расстоянии от измеренной частоты.
         */

        int openKeySize = preSiftedKeySize-preSiftedKey.size();

        if (errorCount>0)
            deltaError = 3*qSqrt((float)errorCount*(openKeySize-errorCount)/openKeySize)/openKeySize;
        AliceErrorEstimation = ((float)errorCount)/(openKeySize);

        if ((10.0*deltaError<AliceErrorEstimation) || (preSiftedKeySize > 2*preSiftedKey.size())) {
            return SendParities;
        }
    }

    // начинаем собирать пакет для пересылки данных

    dataOut <<(quint8) ErrorEstimation;
    dataSubset.clear();

    // Набираем данные для пересылки
    // число данных для случайной выборки
    int count = qCeil((float)preSiftedKey.size()*SIFTED_PART);
    quint32 bitPosition = 0;
    for (int i = 0; i<count; ++i) {

        // выбираем случайное положение бита
        bitPosition = nextRandomBit(bitPosition,preSiftedKey.size());
        dataSubset.append(bitPosition);
    }

    // не утруждаем себя проверкой на повторное включение данных
    // сортировка dataSubset, по возрастанию
    qSort(dataSubset);

    // проверяем на повторы, удаляем все, что повторилось
    for (int i = 0; i<dataSubset.size()-1;){
        if (dataSubset[i] == dataSubset[i+1]) dataSubset.remove(i);
        else ++i;
    }

    // готовим пакет данных к отправке
    dataOut<<(quint32)(dataSubset.size()*(sizeof(quint32)+sizeof(quint8)));
    for (int i = 0; i<dataSubset.size();++i){
        dataOut<<(quint32)dataSubset[i];
        dataOut<<(quint8) preSiftedKey[dataSubset[i]];
    }

    // отправляем данные
    mSocket->write(ba);

    // Прорежаем данные Алисы
    deleteBits(dataSubset);

    return ErrorEstimation;
}

quint8 QKDAlice::sendParitiesProc()
{
    /** Алгоритм работы процедуры следующий:
     *
     * 1. Собираем блок случайных битов. Размер блока должен быть таким, чтобы в нем была примерно 1 ошибка
     *
     * 2. Пересылаем от него в ответ четность полученных битов
     *
     * 3. Проверяем полученный блок на четность.
     * При соответствии четности переходим к пункту к выбору битов для нового блока
     * В противном случае убираем из блока первый бит, разбиваем его на 2 части и
     * рекурсивно продолжаем эту процедуру до тех пор пока четности по всем частям не совпадут
     *
     * Каждый запрос четности сопровождается пометкой к удалению первого из бит в последовательности
     *
     * 4. В конце процедуры удаляем все биты и посылаем бобу сообщение об удалении соответствующих
     * битов в его ключе
     */

    // если достигнутая точность является приемлемой, то останавливаем процедуру рассылки четностей
    if (AliceErrorEstimation <= mParams.RequiredErrorRate)
    {
        return FinishProcess;
    }

    // разбиваем весь ключ Алисы на новые блоки
    // подготавливаем массив Алисиных блоков
    // Общее число бит
    int dataSize = preSiftedKey.size();
    // размер блока определяется таким образом, чтобы в нем была примерно 1 ошибка
    quint32 blockSize = qMin(qCeil(1/AliceErrorEstimation), qCeil((float)dataSize/2.0));

    // количество блоков
    quint32 blockCounts = qFloor((float)dataSize/blockSize);

    Bits2Delete.clear();

    QVector <bool> closedBit; //массив уже занятых бит
    closedBit.resize(dataSize);
    closedBit.fill(false);

    // заполняем блоки номерами бит в случайной последовательности
    int bitPosition = 0;
    int BadBlocks = 0;

    for (int i = 0; i < (int)blockCounts; ++i){

        AliceSubset.clear(); //Devide Alice key in new subsets;

        // самый первый блок отличается по размеру от всех остальных
        // я сделал так, что он имеет большую длину
        if (i) AliceSubset.resize(blockSize);
        else
            AliceSubset.resize(dataSize - blockSize*(blockCounts-1));


        for (int j = 0; j < AliceSubset.size(); ++j){

            // ищем еще не занятый бит
            while (closedBit[bitPosition]){
                ++bitPosition;
                bitPosition %= dataSize;
            }

            // записываем этот бит в соответствующий блок
            AliceSubset[j] = bitPosition;

            // помечаем выбранный бит, чтобы в дальнейшем его не выбирать вновь
            closedBit[bitPosition] = true;

            // выбираем следующий произвольный бит
            bitPosition = nextRandomBit(bitPosition, dataSize);
        }
        // сортируем блок по возрастанию номеров в нем
        // qSort(AliceSubset);

        if (!CheckParities(AliceSubset)) BadBlocks++;

    }

//    qDebug()<<"Correction done";
    emit logMessageGenerated(QString("Correction done\n"));

    // Warning!!!! Как правильно посчитать условие выхода

    AliceErrorEstimation = AliceErrorEstimation*(1 - qExp(-preSiftedKey.size()*AliceErrorEstimation/blockCounts));
//    qDebug()<<"AliceErrorEstimation"<<AliceErrorEstimation;
    emit logMessageGenerated(QString("AliceErrorEstimation %1\n").arg(AliceErrorEstimation));


    // удаляем биты
    qSort(Bits2Delete);
    deleteBits(Bits2Delete);
    QByteArray ba;
    QDataStream out(&ba, QIODevice::WriteOnly);

    out<<(quint8)DeleteBits;
    out<<(quint32)Bits2Delete.size();
    for (int i = 0; i < Bits2Delete.size(); ++i)
        out<<(quint32)Bits2Delete[i];

    mSocket->write(ba);

//    qDebug()<<"осталось"<<preSiftedKey.size()<<"бит";
    emit logMessageGenerated(QString("Осталось %1 бит\n").arg(preSiftedKey.size()));
    if (preSiftedKey.size()<=0) return ErrorAtProcess;
    return SendParities;
}

quint32 QKDAlice::MaxPossibleSecretKeySize()
{
    preSiftedKeySize = preSiftedKey.size();
    theorErrorCorrection = qCeil(-1.0 * preSiftedKeySize *
                                 (AliceErrorEstimation * log(AliceErrorEstimation)/log(2) +
                                  (1 - AliceErrorEstimation) * log(1 - AliceErrorEstimation)/log(2)));

    float expectedDetectorCountRate = 1.0 - exp(-mParams.DetectionEfficiency * mParams.TotalAttenuation * mParams.PhotonNumber);
    float multuphotonProbability = 1 - (1+mParams.PhotonNumber)*exp(-mParams.PhotonNumber);
    float probability2DetectAfterPhotonSubstraction = multuphotonProbability * mParams.DetectionEfficiency;
    float R = (expectedDetectorCountRate - probability2DetectAfterPhotonSubstraction)/expectedDetectorCountRate;
    float M = AliceErrorEstimation/R;
    bits4PrivacyAmplification = qCeil(preSiftedKeySize*(1.0 - R*(1.0 - log(1 + 4*M - 4*M*M)/log(2))));

    //это пока не знаю что такое ???????
    if (bits4PrivacyAmplification < 0 )
    {
        qDebug()<<"Privacy amplification"<<bits4PrivacyAmplification<<"bit from"<<preSiftedKey<<"bit key.";
        emit logMessageGenerated(QString("Privacy amplification %1 bit from %2 bit key.\n").arg(bits4PrivacyAmplification).arg(preSiftedKey.size()));
        return -1;
    }

    int Result = preSiftedKeySize-bits4PrivacyAmplification-theorErrorCorrection;
//    qDebug()<<"Privacy amplification"<<bits4PrivacyAmplification
//           <<"bits, error correction"<<theorErrorCorrection
//          <<"bits from"<<preSiftedKeySize
//         <<"bit key, MAX possible secret key size is"<<Result;
    emit logMessageGenerated(QString("Privacy amplification %1 bits, "
                                     "error correction %2 "
                                     "bits from %3 bit key, "
                                     "MAX possible secret key size is %4\n")
                             .arg(bits4PrivacyAmplification)
                             .arg(theorErrorCorrection)
                             .arg(preSiftedKeySize)
                             .arg(Result));
    return Result;

}

bool QKDAlice::CheckParities(QVector<quint32> BitPositions)
{
    /** Процедура проверки четности.
     * Данная процедура рекурсивная. Вызывается деление массива на 2 части до тех пор пока
     * длина массива не станет равно 0 - в этом случае не надо проводить проверку четности.
     * Также условием прекращения работы процедуры является случай, когда длина массива равна 1.
     * В этом случае не имеет смысла проверять четность, по тому как при этом раскрывается секретный бит.
     * В этом случае нужно просто удалить этот бит из секретного ключа
     */


    if (BitPositions.size() == 0) return true;
    if (BitPositions.size() == 1) {
        Bits2Delete.append(BitPositions[0]);
        return true;
    }

    bool Result = true;
    // Подготавливаем сообщение для отправки Бобу
    QByteArray ba;
    QDataStream out(&ba, QIODevice::WriteOnly);
    QDataStream in(&ba, QIODevice::ReadOnly);

    out << (quint8) SendParities;

    quint8 parities = 0;
    // в начале каждого блока посылаем его длину
    out <<  (quint32) BitPositions.size();
    for (int i = 0; i < BitPositions.size(); ++i){
        // пересылаем все номера данного блока
        out << (quint32) BitPositions[i];
        if (preSiftedKey[BitPositions[i]]) parities = 1 - parities;
    }

    // пересылаем данные Бобу
    mSocket->write(ba);

    //ждем ответа от Боба
    while (mSocket->bytesAvailable()<2*sizeof(quint8)){
        // если данные так и не доходят, то рвем соединение и закрываем сервер
        if (!mSocket->waitForReadyRead(10000)) {
            qDebug()<<"Alice can't take a answer from a Bob";
            break;
        }
    }

    ba.clear();
    if (mSocket->bytesAvailable()<2*sizeof(quint8)) qWarning()<<"Data was lose";
    ba.append(mSocket->read(2*sizeof(quint8)));

    quint8 returnParitie = ba[1];

    // готовим к удалению первый из битов
    Bits2Delete.append(BitPositions[0]);

    if (returnParitie!=parities) {
        // обнаружена ошибка
        // в этом случае убираем первый из наших бит в посылке и повторяем рекурсивно данную процедуру
        Result = false;
        int size = (BitPositions.size()-1)/2;
        if (CheckParities(BitPositions.mid(1,size))) //если ошибка была не в первом блоке, то смотрим во втором
            CheckParities(BitPositions.mid(1+size));
        // вообще возможна ситуация когда ошибочным был именно 0-вой бит, тогда мы потеряем 2 бита
    }
    return Result;
}

void QKDAlice::readRecievedIndexes(int size, QTcpSocket *Socket)
{
    QByteArray tempBa;
    QDataStream in(&tempBa, QIODevice::ReadOnly);
    QVector<rawData> bufferRawData;
    quint32 index;
    int singlePktSize = sizeof(quint32);

    // данных может быть много, поэтому читать тоже будем долго
    while (tempBa.size()<size*singlePktSize){
        if (!Socket->waitForReadyRead(1000)) break;
    }

    // читаем все
    // данных может быть много и за 1 сек можем не управиться.
    // Поэтому
    tempBa.append(Socket->read(size*singlePktSize));

    preSiftedKey.clear();

    // данные получены, прореживаем сырой ключ Алисы/Боба
    // для начала считаем первое посылаемое значение Боба/Алисы
    in >> index;
    --size;

    for (int i = 0; i<mRawKey.size(); ++i) {
        rawData tempData = mRawKey[i];
        // удаляем все значения сырого ключа Алисы до нужного индекса в посылке Боба
        if (index == tempData.pos){
            bufferRawData.append(tempData);
            preSiftedKey.append(tempData.Bit);


            //считываем следующее значение
            if (size>0) { // еще есть данные в посылке
                in >> index;
                --size;
            }
            else index = mRawKey.size();
        }
    }

    mRawKey.clear();
    for (int i = 0; i<bufferRawData.size(); ++i)
        mRawKey.append(bufferRawData[i]);
}

quint8 QKDAlice::ldpcCorrectionProc()
{
    LDPC ldpcForSyndrome;
    ldpcForSyndrome.init("H.alist");

    size_t const CODE_LENGTH = ldpcForSyndrome.n;
    size_t const EXPOSED_PART = CODE_LENGTH / 100;


    // Усекаем текущий ключ до размера, кратного матрице

    preSiftedKey.chop(preSiftedKey.size() % CODE_LENGTH);
    if (preSiftedKey.size() < CODE_LENGTH)
    {
        preSiftedKey.clear();
        emit logMessageGenerated("Длина предпросеянного ключа недостаточна для исправления ошибок!\n");
        return -1;
    }

    preSiftedKeySize = preSiftedKey.size();

    size_t chunksNumber = preSiftedKey.size() / CODE_LENGTH;

    size_t totalIterationsNumber{0};

    for (size_t chunkNumber = 0; chunkNumber < chunksNumber; ++chunkNumber)
    {

        Matrix<FieldElement> aliceVectorField(1, CODE_LENGTH);
        for (size_t i = 0; i < CODE_LENGTH; ++i)
        {
            aliceVectorField(0, i) = FieldElement(preSiftedKey[chunkNumber * CODE_LENGTH + i]);
        }

        Matrix<FieldElement> neededSyndrome;
        ldpcForSyndrome.syndrome(aliceVectorField, neededSyndrome);

        QByteArray ba;
        QDataStream out(&ba,QIODevice::WriteOnly);
        out<<(quint8) SendSyndrome;
        out<<(quint32) neededSyndrome.getColumnsNumber();
        for (size_t i = 0; i < neededSyndrome.getColumnsNumber(); ++i)
        {
            out<<(quint8)neededSyndrome(0, i).getElement();
        }
        mSocket->write(ba);

        QByteArray tempBa;
        while (tempBa.size()<1)
        {
            if (mSocket->waitForReadyRead(1000)) break;
        }
        tempBa.append(mSocket->read(sizeof(quint8)));
        if ((int)(sizeof(quint8))>tempBa.size())
        {
            return ErrorAtProcess;
        }

        QDataStream in(&tempBa, QIODevice::ReadOnly);
        quint8 command;
        in >> command;

        size_t errCorrectionItersNumber{0};
        QSet<size_t> positionsToExpose;
        while (command == ErrCorrectionFailed)
        {
//            emit logMessageGenerated(QString("Запущена %1 итерация корректировки ошибок").arg(errCorrectionItersNumber + 1));
//            qApp->processEvents();

            ++errCorrectionItersNumber;
            while (positionsToExpose.size() < (EXPOSED_PART * (errCorrectionItersNumber)))
            {
                positionsToExpose.insert(qrand() % CODE_LENGTH);
            }
            QByteArray ba;
            QDataStream out(&ba,QIODevice::WriteOnly);
            out << (quint8) ExposeBits;
            out << (quint32) positionsToExpose.size();
            for (size_t position : positionsToExpose)
            {
                out << (quint32) position << (quint8) aliceVectorField(0, position).getElement();
                if (!Bits2Delete.contains(chunkNumber * CODE_LENGTH + position))
                {
                    Bits2Delete.append(chunkNumber * CODE_LENGTH + position);
                }
            }

            mSocket->write(ba);

            QByteArray tempBa;
            while (tempBa.size() < 1)
            {
                if (mSocket->waitForReadyRead(1000)) break;
            }
            tempBa.append(mSocket->read(sizeof(quint8)));
            if ((int)(sizeof(quint8))>tempBa.size())
            {
                return ErrorAtProcess;
            }

            QDataStream in(&tempBa, QIODevice::ReadOnly);
            in >> command;
        }

        emit logMessageGenerated(QString("Блок %1 скорректирован за %2 итераций").arg(chunkNumber).arg(errCorrectionItersNumber));
        totalIterationsNumber += errCorrectionItersNumber;
    }

    QByteArray ba;
    QDataStream out(&ba,QIODevice::WriteOnly);
    out<<(quint8) FinishProcess;
    mSocket->write(ba);

    updateSiftedKey();

    emit logMessageGenerated(QString("Все блоки скорректированы за %1 итераций").arg(totalIterationsNumber));
}

void QKDAlice::strengthenedKeyProcess()
{
    QByteArray ba;
    QDataStream out(&ba,QIODevice::WriteOnly);
    out << (quint8) SendMultiplierAndPoly;

    mSocket->write(ba);

    QByteArray tempBa;
    while (tempBa.size() < 1)
    {
        if (mSocket->waitForReadyRead(1000)) break;
    }
    tempBa.append(mSocket->read(sizeof(quint8)));
    if ((int)(sizeof(quint8))>tempBa.size())
    {
        return;
    }
    quint8 command;
    QDataStream in(&tempBa, QIODevice::ReadOnly);
    in >> command;
    if (command != StrengtheningSuccess)
    {
        emit logMessageGenerated("Усиление ключа завершилось с ошибкой");
        return;
    }

    preSiftedKey.clear();
    QByteArray siftedKey = showSiftedKey();
    for (size_t i = 0; i < getSiftedKeySize(); ++i)
    {
        quint8 mask = 0b10000000;
        for (size_t j = 0; j < 8; ++j)
        {
            preSiftedKey.push_back((siftedKey[i] & mask) >> (7 - j));
            mask >>= 1;
        }
    }

//    std::set<size_t> positionsToErase;
//    while (positionsToErase.size() < Bits2Delete.size())
//    {
//        positionsToErase.insert(qrand() % preSiftedKey.size());
//    }
//    Bits2Delete.clear();

//    for (std::set<size_t>::const_iterator pPosition = positionsToErase.cend(); pPosition != positionsToErase.cbegin(); --pPosition)
//    {
//        preSiftedKey.remove(*pPosition, 1);
//    }

    preSiftedKey.chop(Bits2Delete.size());
    Bits2Delete.clear();

    clearSiftedKey();
    updateSiftedKey();

    emit logMessageGenerated("Усиление ключа завершилось успешно");
}

QKDAlice* mAlice = NULL;

QKDAlice *CreateAliceInterface()
{
    if (mAlice == NULL) mAlice = new QKDAlice();
    return mAlice;

}
