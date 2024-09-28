#ifndef QKDBOB_H
#define QKDBOB_H

#include "qkdunit.h"
#include "qkdlib_global.h"
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkSession>
#include <QMutex>


typedef struct sDetectorClick{
    quint32 pos;
    quint8 detectorNumber;
    quint8 intervalNumber;
} detectorClick;

class QKDLIBSHARED_EXPORT QKDBob : public QKDUnit
{
public:
    QKDBob();
    enum {
        BOB_STATUS_OK = 0x100,
        BOB_STATUS_UNABLE_START_SERVER = 0x101
    };

    //процедура запуска процесса Боба должна запускаться в отдельном потоке
    void StartBobProcess();
    void StopBobProcess();

    QVector<detectorClick> mDetectorClicks;

private:
    int TCPIPserverStart();
    int TCPIPserverStop();
    quint8 ErrorEstimationProc(quint32 size);
    quint8 ldpcCorrectionProc(quint32 size, QTcpSocket* Socket);
    quint8 continueLDPCCorrection(quint32 size, QTcpSocket* Socket);
    quint8 strengthenedKeyProc(quint32 size, QTcpSocket* Socket);
    void CalculateParities(quint32 size);
    void DeleteWrongBits(quint32 size);
    void readIndexesAndBasis(int size, QTcpSocket* Socket);
    void writeRecievedIndexes(quint8 Direction, QTcpSocket *Socket);
    void writeRightClickedIndexes(quint8 Direction, QTcpSocket *Socket);

    QTcpServer *mServer;
    QTcpSocket *mSocket;
    bool mStopProcess;
    QMutex *stopMutex;
    size_t currentCorrectingChunkNumber;
    std::vector<int> currentSyndrome;
    QVector<quint32> Bits2Delete;
};

QKDBob* QKDLIBSHARED_EXPORT CreateBobInterface();

#endif // QKDBOB_H
