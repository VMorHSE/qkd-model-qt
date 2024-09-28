#include "qkdunit.h"
#include <QDebug>
#include <QMutexLocker>
#include <QDataStream>

QKDUnit::QKDUnit():
    mMaxSize(LONG_MAX),
    mutex(new QMutex())
{
}

void QKDUnit::setTCPIPserver(const QString address, const int port){
    setTCPIPaddress(address);
    setPort(port);
}

void QKDUnit::setTCPIPaddress(const QString address){mTCPIPaddress = address;}

QString QKDUnit::TCPIPaddress(){return mTCPIPaddress;}

void QKDUnit::setPort(const int port){mPort = port;}

int QKDUnit::port(){return mPort;}

void QKDUnit::setMaxSize(const quint32 size){mMaxSize = size;}

quint32 QKDUnit::maxSize(){return mMaxSize;}

int QKDUnit::getSiftedKeySize()
{
    if (!sharedMemory.isAttached()) sharedMemory.attach();

    sharedMemory.lock();

    quint32 size = 0;

    if  (sharedMemory.size()){
        const char* from = (const char*)sharedMemory.data();
        size = *((quint32*) from);
//        qDebug()<<"Data size:"<< size;
//        emit logMessageGenerated(QString("Data size: %1\n").arg(size));
    }

    sharedMemory.unlock();

    return size;
}

int QKDUnit::getSiftedKey(const quint32 size, QByteArray *siftedKey)
{
    QMutexLocker locker(mutex);
    loadFromSharedMemory();

    quint32 KeySize = mSiftedKey.size();
    if (size>KeySize) return QKD_STATUS_NOT_ENOTH_KEY_DATA;

    siftedKey->clear();
    siftedKey->append(mSiftedKey.data(), size);

    KeySize-=size;

    mSiftedKey = mSiftedKey.right(KeySize);

    writeToSharedMemory();
    return QKD_STATUS_OK;
}

QByteArray QKDUnit::showSiftedKey()
{
    return mSiftedKey;
}

void QKDUnit::writeIndexesAndBasis(quint8 Direction, QTcpSocket *Socket)
{
    QByteArray ba;
    QDataStream out(&ba,QIODevice::WriteOnly);
    out<<(quint8) Direction;
    out<<(quint32) mRawKey.size();
    for (int i = 0; i < mRawKey.size(); ++i) {
        out<<(quint32)mRawKey[i].pos;
        out<<(quint8)mRawKey[i].Base;
    }
    Socket->write(ba);
}

void QKDUnit::readIndexesAndBasis(int size, QTcpSocket* Socket)
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

    for (int i = 0; i<mRawKey.size(); ++i) {
        rawData tempData = mRawKey[i];
        // удаляем все значения сырого ключа Алисы до нужного индекса в посылке Боба
        if (index == tempData.pos){

            // при совпадении базисов записываем значение
            if (Base == tempData.Base) {
                bufferRawData.append(tempData);
                preSiftedKey.append(tempData.Bit);
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

void QKDUnit::deleteBits(QVector<quint32> bitPosition)
{
    // Делаем это задом наперед, поскольку при этом не происходит смещения бит данных
    for (int i = bitPosition.size()-1; i>=0; --i) preSiftedKey.remove(bitPosition[i],1);
}

void QKDUnit::updateSiftedKey()
{
    QMutexLocker locker(mutex);
    // обновляем ключ
    loadFromSharedMemory();

    if (mSiftedKey.size()> mMaxSize) return;
    int NewKeyByteCount = preSiftedKey.size()/8;
    for (int i = 0; i<NewKeyByteCount;++i){
        quint8 tempbyte = 0;
        for (int j = 0; j < 8; ++j){
            tempbyte = tempbyte << 1;
            if (preSiftedKey[i*8 + j]) ++tempbyte;
        }
        mSiftedKey.append(tempbyte);
    }
    preSiftedKey.clear();

    // загружаем в память
    writeToSharedMemory();

//    qDebug()<<"Sifted key size: "<<mSiftedKey.size();
    emit logMessageGenerated(QString("Просеянный ключ обновлён. Размер просеянного ключа: %1 байт.\n").arg(mSiftedKey.size()));
}

void QKDUnit::clearSiftedKey()
{
    mSiftedKey.clear();
    writeToSharedMemory();
}

void QKDUnit::Test()
{
//    qDebug()<<"Test";
    emit logMessageGenerated(QString("Test\n"));
}

void QKDUnit::loadFromSharedMemory()
{
    if (!sharedMemory.isAttached()) sharedMemory.attach();

    sharedMemory.lock();
    int memsize = sharedMemory.size();

    quint32 size = 0;
    const char* from = (const char *)sharedMemory.data();

    if (sharedMemory.size()) {

        size = *((quint32*) from);
        from += sizeof(quint32);
    }

    mSiftedKey.clear();
    mSiftedKey.append(from, size);
    sharedMemory.unlock();

}

void QKDUnit::writeToSharedMemory()
{
    if (!sharedMemory.isAttached()) sharedMemory.attach();

    quint32 size = mSiftedKey.size();

    sharedMemory.lock();

    char* to = (char*)sharedMemory.data();
    memcpy(to, &size, sizeof(quint32));
    to += sizeof(quint32);

    memcpy(to, (const char*) mSiftedKey.data(),
           qMin(sharedMemory.size() - sizeof(quint32), size));
    sharedMemory.unlock();
}
