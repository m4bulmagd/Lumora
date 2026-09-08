#pragma once
// Private tool implementation; never included by Processing/Core public headers.
#include "EvidenceSupport.hpp"
#include <lumora/processing/ProcessingPreparation.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace lumora::evidence {
inline QString qs(const std::string& s) {return QString::fromStdString(s);}
inline QJsonValue integer(std::uint64_t n) {if(n>static_cast<std::uint64_t>(INT64_MAX)) throw Error(3,"JSON integer overflow");return QJsonValue(static_cast<qint64>(n));}
inline std::string json(const QJsonObject& o) {return QJsonDocument(o).toJson(QJsonDocument::Indented).toStdString();}
QJsonObject strictObject(const std::string&);
QJsonObject pipelineJson(const processing::PipelineDefinition&);
QJsonObject parametersJson(const processing::StageDefinition&);
QString stageId(processing::StageId);
QJsonObject dimensions(std::uint32_t,std::uint32_t);
QJsonObject orientationJson(core::Orientation);
QJsonObject descriptorJson(const core::SourcePixelFormat&,const core::ImageLayout&);
QJsonObject provenance();
QJsonObject execution();
QString utcNow();
QJsonObject sessionResources(const processing::ProcessingResources&);
QJsonObject standaloneResources(std::size_t scratch,std::size_t fixed,std::size_t imageBytes);
QJsonObject payloadJson(const std::filesystem::path& directory,const std::string& file,const Image&,bool gray8=false);
const std::vector<std::string>& rowIds();
}
