#ifndef QKDUNIT_H
#define QKDUNIT_H

#include "qkdlib_global.h"
#include <QString>
#include <QVector>
#include <QBitArray>
#include <QTcpSocket>
#include <QMutexLocker>
#include <QSharedMemory>
#include <set>

// тип
typedef struct sRawData{
    quint32 pos;
    char Base;
    char Bit;
} rawData;

typedef struct sQKDprotokoldata{
    char packetType;
    char datasize;
    char* data;
} QkdProtokolData;

class QKDLIBSHARED_EXPORT QKDUnit : public QObject
{
    Q_OBJECT

public:
    QKDUnit();

    // Status
    enum {
        QKD_STATUS_OK = 0,
        QKD_STATUS_OVERFLOW = 1,
        QKD_STATUS_NOT_ENOTH_KEY_DATA = 2
    };

    // type of packets
    enum {
        NoType = 0,
        Request2IndexesAndBases = 1,
        SetIndexesAndBasis = 2,
        ClearRawData = 3,
        ErrorEstimation = 4,
        SendParities = 5,
        ErrorAtProcess = 6,
        FinishProcess = 7,
        DeleteBits = 8,
        RequestRecievedIndexes = 9,
        RequestRightClickedIndexes = 10,
        SendSyndrome = 11,
        ErrCorrectionSuccess = 12,
        ErrCorrectionFailed = 13,
        ExposeBits = 14,
        SendMultiplierAndPoly = 15,
        StrengtheningSuccess = 16
    };

    void setTCPIPserver(const QString address, const int port);

    void setTCPIPaddress(const QString address);
    QString TCPIPaddress();

    void setPort(const int port);
    int port();

    void setMaxSize(const quint32 size);
    quint32 maxSize();

    int getSiftedKeySize();
    int getSiftedKey(const quint32 size, QByteArray *siftedKey);
    QByteArray showSiftedKey();

    void writeIndexesAndBasis(quint8 Direction, QTcpSocket *Socket);
    void readIndexesAndBasis(int size, QTcpSocket *Socket);

    void deleteBits(QVector<quint32> bitPosition);

    void updateSiftedKey();

    void clearSiftedKey();

    void Test();

    QVector<rawData> mRawKey;
    QByteArray preSiftedKey;
    QSharedMemory sharedMemory;
    double errorRate;

signals:
    void logMessageGenerated(const QString& message);

private:
    QString mTCPIPaddress;
    int mPort;
    quint32 mMaxSize;
    QMutex *mutex;
    QByteArray mSiftedKey;
    void loadFromSharedMemory();
    void writeToSharedMemory();
};

#endif // QKDUNIT_H
