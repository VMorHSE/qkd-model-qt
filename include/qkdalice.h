#ifndef QKDALICE_H
#define QKDALICE_H

#include <qkdunit.h>
#include <QObject>
#include "qkdlib_global.h"
#include <QTcpSocket>

#define SIFTED_PART 0.01f

typedef struct sSystemParams {
    float TotalAttenuation;
    float DetectionEfficiency;
    float PhotonNumber;
    float RequiredErrorRate;
}SystemParams;

class QKDLIBSHARED_EXPORT QKDAlice : public QKDUnit
{
public:
    QKDAlice();
    void connectToServer();
    bool isConnected();
    void rawData2preSiftedKey();
    void siftedKeyProcess();
    void strengthenedKeyProcess();


    void setSystemParams(const SystemParams Params);
    void setTotalAttenuation(const float value);
    void setDetectionEfficiency(const float value);
    void setPhotonNumber(const float value);
    void setRequiredErrorRate(const float value);
    SystemParams getSystemParams();

private:
    quint8 errorEstimationProc();
    quint8 sendParitiesProc();
    quint8 ldpcCorrectionProc();
    quint32 MaxPossibleSecretKeySize();
    bool CheckParities(QVector<quint32> BitPositions);
    void readRecievedIndexes(int size, QTcpSocket *Socket);
    QTcpSocket *mSocket;
    int preSiftedKeySize, errorCount;
    float AliceErrorEstimation;
    int bits4PrivacyAmplification; //Number of bits spent for privacy amplification
    int theorErrorCorrection; //Theoretical estimation of number of bits for error correction
    SystemParams mParams;
    QVector<quint32> AliceSubset, Bits2Delete;


};

QKDAlice* QKDLIBSHARED_EXPORT CreateAliceInterface();

#endif // QKDALICE_H
