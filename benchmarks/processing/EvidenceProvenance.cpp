#include "EvidenceJson.hpp"
#include "EvidenceBuild.hpp"
#include <QSysInfo>
#include <QSettings>
#include <QThread>
#include <opencv2/core.hpp>
#include <cfenv>
#include <fstream>
namespace lumora::evidence {
namespace { QJsonValue nullable(const QString& s) {return s.isEmpty()?QJsonValue():QJsonValue(s);} }
QJsonObject provenance() {
    auto build=strictObject(EVIDENCE_CONFIG);QJsonValue cpu;
#if defined(__linux__)
    std::ifstream stream("/proc/cpuinfo");std::string line;while(std::getline(stream,line)) if(line.starts_with("model name")) {const auto colon=line.find(':');if(colon!=std::string::npos) cpu=qs(line.substr(colon+2));break;}
#elif defined(_WIN32)
    QSettings processor(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),QSettings::NativeFormat);
    cpu=nullable(processor.value(QStringLiteral("ProcessorNameString")).toString().trimmed());
#endif
    const auto cores=QThread::idealThreadCount();
    QJsonValue dirty;
#if EVIDENCE_DIRTY >= 0
    dirty=EVIDENCE_DIRTY!=0;
#endif
    return {{"source",QJsonObject{{"revision",nullable(EVIDENCE_REVISION)},{"dirty",dirty},{"dirtyScope","git-status-porcelain-v1-untracked-normal"},{"statusSha256",nullable(EVIDENCE_STATUS_SHA)},{"capturedUtc",EVIDENCE_CAPTURED}}},
        {"build",build},{"host",QJsonObject{{"osName",nullable(QSysInfo::productType())},{"osVersion",nullable(QSysInfo::productVersion())},{"architecture",nullable(QSysInfo::currentCpuArchitecture())},{"cpuModel",cpu},{"logicalCores",cores>0?QJsonValue(cores):QJsonValue()}}},
        {"dependencies",QJsonObject{{"opencvVersion",qs(cv::getVersionString())},{"opencvVcpkgPortVersion",nullable(LUMORA_OPENCV_PORT_VERSION)},{"vcpkgBaseline",nullable(LUMORA_VCPKG_BASELINE)},{"qtVersion",qVersion()}}}};
}
QJsonObject execution() {
    return {{"backend","cpu"},{"threadModel","Lumora prepared session: fixed 1-4 execution slots including caller, 0-3 persistent helpers; current image stages remain serial; private synchronous helper controls only; standalone stages serial"},{"opencvThreads",cv::getNumThreads()},{"roundingMode",std::fegetround()==FE_TONEAREST?"FE_TONEAREST":"unsupported"},{"algorithmImplementation","Lumora prepared CLAHE; OpenCV coefficient generation and Lumora separable-double Gaussian; Lumora prepared Median/Sharpen"},{"algorithmProvenance","Lumora-owned code (Apache-2.0); adapted OpenCV 4.12.0 CLAHE source retains NVIDIA 2013/Itseez 2014 three-clause BSD terms; pinned coefficient generation; see THIRD-PARTY-LICENSES/OpenCV-CLAHE.txt"}};
}
}
