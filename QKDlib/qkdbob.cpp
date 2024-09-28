#include "qkdbob.h"
#include "LDPCCorrect.hpp"
#include "GaloisComputer.hpp"
#include <QDataStream>

QKDBob::QKDBob():
    QKDUnit(),
    mServer(NULL),
    mSocket(NULL),
    mStopProcess(false),
    stopMutex(new QMutex()),
    currentCorrectingChunkNumber(0)
{
    sharedMemory.setKey("BobSecretKey");
    if (sharedMemory.isAttached()) sharedMemory.detach();
    if (!sharedMemory.create(1024*1024)){ //1MB
//        qDebug()<<"Unable to create shared memory segment. Error:"<<sharedMemory.errorString();
        emit logMessageGenerated(QString("Unable to create shared memory segment. Error: %1\n").arg(sharedMemory.errorString()));
        return;
    }
}

/**
 * основной процесс Боба
 */
void QKDBob::StartBobProcess()
{
    mStopProcess = false;

    // запускаем сервер
    TCPIPserverStart();

    // что-то пошло не так
    if (mServer == NULL) return;

    // бесконечный цикл работы процесса
    while (true) {

        // ждем только первого подключения
        if (mSocket == NULL){
            if (mServer->waitForNewConnection(100))
            {
                // Подключение произошло, запоминаем подключившийся сокет
                stopMutex->lock();
//                qDebug()<<"New Connection";
                emit logMessageGenerated(QString("New Connection\n"));
                mSocket = mServer->nextPendingConnection();
                stopMutex->unlock();
            }
        }

        // в случае подключенного сокета ждем сигнала от него,
        // который в дальнейшем будем обрабатывать
        else {
            stopMutex->lock();
            // что то уже есть в буфере сокета
            if (mSocket->bytesAvailable()) {

                // пришел новый пакет данных
                int estimatedPacketSize = 0;

                // Считываем 1 байт данных, который определяет нам тип
                // пересылаемого пакета данных
                QByteArray ba = mSocket->read(1);
                quint8 cmd = ba[0];
                QDataStream in(&ba, QIODevice::ReadOnly);

                // определяем ожидаемую длину пакета
                switch (cmd) {
                case Request2IndexesAndBases:
                case ClearRawData:
                case ErrorAtProcess:
                case FinishProcess:
                    estimatedPacketSize = 0;
                    break;
                case SetIndexesAndBasis:
                case ErrorEstimation:
                case SendParities:
                case DeleteBits:
                case SendSyndrome:
                case ExposeBits:
//                case SendMultiplierAndPoly:
                    estimatedPacketSize = sizeof(quint32); //размер длины посылки
                    break;
                default:
                    estimatedPacketSize = 0;
                }

                // дожидаемся оставшихся данных
                while (mSocket->bytesAvailable()<estimatedPacketSize){
                    // если данные так и не доходят, то рвем соединение и закрываем сервер
                    if (!mSocket->waitForReadyRead(10000)) {
                        mSocket->disconnectFromHost();
                        break;
                    }
                }

                ba.clear();

                // если длина пакета данных более
                if (estimatedPacketSize) ba.append(mSocket->read(estimatedPacketSize));

                //обработка команды
                quint32 bufferSize;
                switch (cmd) {
                case RequestRecievedIndexes:
                    writeRecievedIndexes(RequestRecievedIndexes, mSocket);
                    break;
                case RequestRightClickedIndexes:
                    writeRightClickedIndexes(RequestRightClickedIndexes, mSocket);
                    break;
                case Request2IndexesAndBases:
                    writeIndexesAndBasis(Request2IndexesAndBases, mSocket);
                    break;
                case SetIndexesAndBasis:
                    in>>bufferSize;
                    readIndexesAndBasis(bufferSize, mSocket);
                    break;
                case ClearRawData:
                    mRawKey.clear();
                    break;
                case ErrorEstimation:
                    in>>bufferSize;
                    ErrorEstimationProc(bufferSize);
                    break;
                case ErrorAtProcess:
                    preSiftedKey.clear();
                    break;
                case SendParities:
                    in>>bufferSize;
                    CalculateParities(bufferSize);
                    break;
                case DeleteBits:
                    in>>bufferSize;
                    DeleteWrongBits(bufferSize);
                    break;
                case FinishProcess:
                    currentCorrectingChunkNumber = 0;

//                    std::sort(Bits2Delete.begin(), Bits2Delete.end());
//                    for (int i = Bits2Delete.size() - 1; i >= 0; --i)
//                    {
//                        preSiftedKey.remove(Bits2Delete[i], 1);
//                    }
//                    Bits2Delete.clear();

                    updateSiftedKey();
                    break;
                case SendSyndrome:
                    in>>bufferSize;
                    ldpcCorrectionProc(bufferSize, mSocket);
                    break;
                case ExposeBits:
                    in>>bufferSize;
                    continueLDPCCorrection(bufferSize, mSocket);
                    break;
                case SendMultiplierAndPoly:
                    in>>bufferSize;
                    strengthenedKeyProc(bufferSize, mSocket);
                    break;
                }
            }

            // если буфер пуст пытаемся прочитать данные
            else mSocket->waitForReadyRead(100);
            stopMutex->unlock();
        }

        // остановка процесса
        QMutexLocker locker(stopMutex);
        if (mStopProcess) {
            break;
        }
    }

    TCPIPserverStop();
}

void QKDBob::StopBobProcess()
{
//    qDebug()<<"Try to stop process";
    emit logMessageGenerated(QString("Try to stop process\n"));
    QMutexLocker locker(stopMutex);
    mStopProcess = true;
}

int QKDBob::TCPIPserverStart()
{
    if (mServer == NULL){
        mServer = new QTcpServer();
    };

    if (!mServer->isListening() && !mServer->listen(QHostAddress(TCPIPaddress()), port()))
        return BOB_STATUS_UNABLE_START_SERVER;

//    qDebug()<<"Server was started";
    emit logMessageGenerated(QString("Server was started\n"));
    return BOB_STATUS_OK;
}

int QKDBob::TCPIPserverStop()
{
    if (mServer!=NULL) mServer->deleteLater();
    mServer = NULL;
//    qDebug()<<"Server was stoped";
    emit logMessageGenerated(QString("Server was stoped\n"));
    return BOB_STATUS_OK;
}

quint8 QKDBob::ErrorEstimationProc(quint32 size)
{
    // Считываем прeдназначенные нам данные
    QByteArray ba;
    QDataStream dataIn(&ba, QIODevice::ReadOnly);
    QDataStream dataOut(&ba, QIODevice::WriteOnly);
    while (mSocket->bytesAvailable()<size){
        // если данные так и не доходят, то рвем соединение и закрываем сервер
        if (!mSocket->waitForReadyRead(10000)) {
            break;
        }
    }
    ba.append(mSocket->read(mSocket->bytesAvailable()));
    // в  случае возникновения ошибки
    if (ba.size()!= (int)size) {//это ошибка
        ba.clear();
        dataOut<<(quint8) ErrorAtProcess;
        mSocket->write(ba);
        return ErrorAtProcess;
    }

    //Если все в порядке то начинаем считывать данные и
    QVector<quint32> dataPos;
    quint32 errorCount = 0;
    for (int dataCount = 0; dataCount<(int)(size/(sizeof(quint32)+sizeof(quint8))); ++dataCount){
        quint32 temppos;
        quint8 tempbit;
        dataIn >> temppos;
        dataIn >> tempbit;

        dataPos.append(temppos);
        if (preSiftedKey[temppos]!= (char) tempbit) ++errorCount;
    }

    // Отсылаем назад соообщение о найденных ошибках
    ba.clear();
    dataOut << (quint8)ErrorEstimation;
    dataOut << (quint32)errorCount;
    mSocket->write(ba);

    // удаляем номера всех присланных битов
    deleteBits(dataPos);
    return ErrorEstimation;
}

void QKDBob::CalculateParities(quint32 size)
{
    // Считываем прeдназначенные нам данные
    QByteArray ba;
    QDataStream dataIn(&ba, QIODevice::ReadOnly);
    QDataStream dataOut(&ba, QIODevice::WriteOnly);

    //считываем все предназначенные нам данные
    while (mSocket->bytesAvailable()<size*sizeof(quint32)){
        // если данные так и не доходят, то рвем соединение и закрываем сервер
        if (!mSocket->waitForReadyRead(10000)) {
//            qDebug()<<"Bob can't take full data from Alice";
            emit logMessageGenerated(QString("Bob can't take full data from Alice\n"));
            break;
        }
    }
    ba.append(mSocket->read(size*sizeof(quint32)));

    // высчитываем четность присланных нам бит
    quint8 parities = 0;
    for (int i = 0; i < (int) size; ++i){
        int position;
        dataIn >> position;
        if (preSiftedKey[position]) parities = 1 - parities;
    };

    //Полученную четность необходимо переслать назад
    ba.clear();
    dataOut << (quint8) SendParities;
    dataOut << parities;
    mSocket->write(ba);
}

void QKDBob::DeleteWrongBits(quint32 size)
{
    QByteArray ba;
    QDataStream dataIn(&ba, QIODevice::ReadOnly);
    //считываем все предназначенные нам данные
    while (mSocket->bytesAvailable()<size*sizeof(quint32)){
        // если данные так и не доходят, то рвем соединение и закрываем сервер
        if (!mSocket->waitForReadyRead(10000)) {
            break;
        }
    }
    ba.append(mSocket->read(size*sizeof(quint32)));

    QVector<quint32> bitPosition;
    bitPosition.resize(size);

    for (int i = 0; i < (int) size; ++i) {
        dataIn >> bitPosition[i];
    }

    deleteBits(bitPosition);
}

void QKDBob::writeRecievedIndexes(quint8 Direction, QTcpSocket *Socket)
{
    QByteArray ba;
    QDataStream out(&ba,QIODevice::WriteOnly);
    out<<(quint8) Direction;
    quint32 size = mDetectorClicks.size();
    out << size;
    for (int i = 0; i < mDetectorClicks.size(); ++i) {
        out<<(quint32)mDetectorClicks[i].pos;
    }
    Socket->write(ba);
}

void QKDBob::writeRightClickedIndexes(quint8 Direction, QTcpSocket *Socket)
{
    QByteArray ba;
    QDataStream out(&ba,QIODevice::WriteOnly);
    out<<(quint8) Direction;
    out<<(quint32) mRawKey.size();
    for (int i = 0; i < mRawKey.size(); ++i) {
        out<<(quint32)mRawKey[i].pos;
    }
    Socket->write(ba);
}

void QKDBob::readIndexesAndBasis(int size, QTcpSocket* Socket)
{
    QByteArray tempBa;
    QDataStream in(&tempBa, QIODevice::ReadOnly);
    QVector<rawData> bufferRawData;
    quint32 index;
    quint8 Base;
    int singlePktSize = sizeof(quint32)+sizeof(quint8);

    // данных может быть много, поэтому читать тоже будем долго
    while (tempBa.size()<size*singlePktSize){
        if (!Socket->waitForReadyRead(1000)) break;
    }

    // читаем все
    // данных может быть много и за 1 сек можем не управиться.
    // Поэтому
    tempBa.append(Socket->read(size*singlePktSize));

    // данные получены, прореживаем сырой ключ Алисы/Боба
    // для начала считаем первое посылаемое значение Боба/Алисы
    in >> index;
    in >> Base;
    --size;

    for (int i = 0; i < mDetectorClicks.size(); ++i) {
        detectorClick click = mDetectorClicks[i];
        // удаляем все значения сырого ключа Алисы до нужного индекса в посылке Боба
        if (index == click.pos)
        {
            rawData tempdata;
            tempdata.pos = index;
            tempdata.Base = Base;

            // Если детектор щёлкнул правильно (без неопределённости), записываем бит. Иначе пропускаем.
            switch (Base)
            {
                case 0:
                {
                    if (click.intervalNumber == 0)
                    {
                        tempdata.Bit = 0;
                        bufferRawData.append(tempdata);
                        preSiftedKey.append((quint8)0);
                    }
                    else if (click.intervalNumber == 2)
                    {
                        tempdata.Bit = 1;
                        bufferRawData.append(tempdata);
                        preSiftedKey.append(1);
                    }
                    break;
                }
                case 1:
                {
                    if (click.detectorNumber == 0 && click.intervalNumber == 1)
                    {
                        tempdata.Bit = 0;
                        bufferRawData.append(tempdata);
                        preSiftedKey.append((quint8)0);
                    }
                    else if (click.detectorNumber == 1 && click.intervalNumber == 1)
                    {
                        tempdata.Bit = 1;
                        bufferRawData.append(tempdata);
                        preSiftedKey.append(1);
                    }
                }
            }

            //считываем следующее значение
            if (size>0) { // еще есть данные в посылке
                in >> index;
                in >> Base;
                --size;
            }
            else index = mRawKey.size();
        }
    }

    mRawKey.clear();
    for (int i = 0; i<bufferRawData.size(); ++i)
        mRawKey.append(bufferRawData[i]);
}

quint8 QKDBob::ldpcCorrectionProc(quint32 size, QTcpSocket* Socket)
{
    LDPC ldpcForSyndrome;
    ldpcForSyndrome.init("H.alist");

    size_t const CODE_LENGTH = ldpcForSyndrome.n;
    size_t const EXPOSED_PART = CODE_LENGTH / 100;

    if (preSiftedKey.size() % CODE_LENGTH)
    {
        // Усекаем текущий ключ до размера, кратного матрице
        preSiftedKey.chop(preSiftedKey.size() % CODE_LENGTH);
    }

    QByteArray tempBa;
    QDataStream in(&tempBa, QIODevice::ReadOnly);

    while (tempBa.size() < size)
    {
        if (!Socket->waitForReadyRead(1000))
        {
            break;
        }
    }

    tempBa = Socket->read(size);

    std::vector<int> neededSyndrome(size);
    for (size_t i = 0; i < size; ++i)
    {
        quint8 b;
        in >> b;
        neededSyndrome[i] = b;
    }

    currentSyndrome = neededSyndrome;

    std::vector<int> noisedData;
    noisedData.reserve(CODE_LENGTH);
    for (size_t i = 0; i < CODE_LENGTH; ++i)
    {
        noisedData.push_back(preSiftedKey[currentCorrectingChunkNumber * CODE_LENGTH + i]);
    }

    std::vector<int> clearedData;
    LDPCCorrect(neededSyndrome, noisedData, clearedData, errorRate);

    Matrix<FieldElement> aliceVectorField(1, CODE_LENGTH);
    for (size_t i = 0; i < CODE_LENGTH; ++i)
    {
        aliceVectorField(0, i) = FieldElement(clearedData[i]);
    }
    Matrix<FieldElement> resultingSyndrome;
    ldpcForSyndrome.syndrome(aliceVectorField, resultingSyndrome);
    bool noErrorsInSyndrome{true};
    for (size_t i = 0; i < resultingSyndrome.getColumnsNumber(); ++i)
    {
        if (resultingSyndrome(0, i).getElement() != neededSyndrome[i])
        {
            noErrorsInSyndrome = false;
            break;
        }
    }

    if (noErrorsInSyndrome)
    {
        for (size_t i = 0; i < clearedData.size(); ++i)
        {
            preSiftedKey[currentCorrectingChunkNumber * CODE_LENGTH + i] = clearedData[i];
        }

        tempBa.clear();
        tempBa.append((char)ErrCorrectionSuccess);
        mSocket->write(tempBa);

        ++currentCorrectingChunkNumber;

        return ErrCorrectionSuccess;
    }
    else
    {
        tempBa.clear();
        tempBa.append((char)ErrCorrectionFailed);
        mSocket->write(tempBa);

        return ErrCorrectionFailed;
    }
}

quint8 QKDBob::continueLDPCCorrection(quint32 size, QTcpSocket* Socket)
{
    LDPC ldpcForSyndrome;
    ldpcForSyndrome.init("H.alist");

    size_t const CODE_LENGTH = ldpcForSyndrome.n;
    size_t const EXPOSED_PART = CODE_LENGTH / 100;

    QByteArray tempBa;
    QDataStream in(&tempBa, QIODevice::ReadOnly);

    while (tempBa.size() < size)
    {
        if (!Socket->waitForReadyRead(1000))
        {
            break;
        }
    }

    tempBa = Socket->read(size * 5); // 4 байта на номер позиции и 1 байт на значение бита

    std::vector<size_t> positions;
    positions.reserve(size);
    std::vector<uint8_t> bits;
    bits.reserve(size);
    for (size_t i = 0 ; i < size; ++i)
    {
        size_t position;
        in >> position;
        quint8 bit;
        in >> bit;
        positions.push_back(position);
        bits.push_back(bit);

        if (!Bits2Delete.contains(currentCorrectingChunkNumber * CODE_LENGTH + position))
        {
            Bits2Delete.append(currentCorrectingChunkNumber * CODE_LENGTH + position);
        }
    }

    std::vector<int> noisedData;
    noisedData.reserve(CODE_LENGTH);
    for (size_t i = 0; i < CODE_LENGTH; ++i)
    {
        noisedData.push_back(preSiftedKey[currentCorrectingChunkNumber * CODE_LENGTH + i]);
    }

    std::vector<int> clearedData;
    LDPCCorrectWithExposedBits(currentSyndrome, noisedData, clearedData, errorRate, positions, bits);

    Matrix<FieldElement> aliceVectorField(1, CODE_LENGTH);
    for (size_t i = 0; i < CODE_LENGTH; ++i)
    {
        aliceVectorField(0, i) = FieldElement(clearedData[i]);
    }
    Matrix<FieldElement> resultingSyndrome;
    ldpcForSyndrome.syndrome(aliceVectorField, resultingSyndrome);
    bool noErrorsInSyndrome{true};
    for (size_t i = 0; i < resultingSyndrome.getColumnsNumber(); ++i)
    {
        if (resultingSyndrome(0, i).getElement() != currentSyndrome[i])
        {
            noErrorsInSyndrome = false;
            break;
        }
    }

    if (noErrorsInSyndrome)
    {
        for (size_t i = 0; i < clearedData.size(); ++i)
        {
            preSiftedKey[currentCorrectingChunkNumber * CODE_LENGTH + i] = clearedData[i];
        }

        tempBa.clear();
        tempBa.append((char)ErrCorrectionSuccess);
        mSocket->write(tempBa);

        ++currentCorrectingChunkNumber;

        return ErrCorrectionSuccess;
    }
    else
    {
        tempBa.clear();
        tempBa.append((char)ErrCorrectionFailed);
        mSocket->write(tempBa);

        return ErrCorrectionFailed;
    }
}

//quint8 QKDBob::strengthenedKeyProc(quint32 size, QTcpSocket* Socket)
//{
//    QByteArray tempBa;
//    QDataStream in(&tempBa, QIODevice::ReadOnly);
//    while (tempBa.size() < size)
//    {
//        if (!Socket->waitForReadyRead(1000))
//        {
//            break;
//        }
//    }
//    tempBa = Socket->read(size);

//    std::vector<uint8_t> multiplier;
//    multiplier.reserve(size);
//    for (size_t i = 0; i < size; ++i)
//    {
//        uint8_t bit;
//        in >> bit;
//        multiplier.push_back(bit);
//    }

//    std::vector<uint8_t> keyAsPoly;
//    keyAsPoly.reserve(getSiftedKeySize() * 8);
//    QByteArray siftedKey = showSiftedKey();
//    for (size_t i = 0; i < getSiftedKeySize(); ++i)
//    {
//        quint8 mask = 0b10000000;
//        for (size_t j = 0; j < 8; ++j)
//        {
//            keyAsPoly.push_back((siftedKey[i] & mask) >> (7 - j));
//            mask >>= 1;
//        }
//    }

//    std::vector<uint8_t> divider(keyAsPoly.size(), 0);
//    divider[0] = 1;
//    divider.back() = 1;
//    divider[divider.size() - 2] = 1;

//    GaloisComputer gComputer;
//    std::vector<uint8_t> strengthenedKey;
//    strengthenedKey = gComputer.GetRemainder(gComputer.Multiply(keyAsPoly, multiplier), divider);

//    preSiftedKey.clear();
//    for (uint8_t bit : strengthenedKey)
//    {
//        preSiftedKey.append(bit);
//    }
//    clearSiftedKey();
//    updateSiftedKey();

//    QByteArray ba;
//    QDataStream out(&ba,QIODevice::WriteOnly);
//    out << (quint8) StrengtheningSuccess;
//    mSocket->write(ba);

//    return StrengtheningSuccess;
//}

quint8 QKDBob::strengthenedKeyProc(quint32 size, QTcpSocket* Socket)
{
    // Удаление со случайных позиций количества бит, которые были раскрыты при исправлении ошибок

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

    QByteArray ba;
    QDataStream out(&ba,QIODevice::WriteOnly);
    out << (quint8) StrengtheningSuccess;
    mSocket->write(ba);

    return StrengtheningSuccess;
}

QKDBob* mBob = NULL;

QKDBob *CreateBobInterface()
{
    if (mBob == NULL) mBob = new QKDBob();
    return mBob;
}
