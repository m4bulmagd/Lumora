#include "EvidenceJson.hpp"
#include <QDateTime>
#include <QJsonParseError>
#include <set>
#include <type_traits>
namespace lumora::evidence {
namespace {
// Syntax comes from Qt; this bounded token walk additionally checks decoded member
// names before QJsonObject has a chance to collapse duplicate keys.
class DuplicateKeys {
    const std::string& s; std::size_t i=0;
    void ws() {while(i<s.size() && (s[i]==' ' || s[i]=='\n' || s[i]=='\r' || s[i]=='\t')) ++i;}
    QString string() {const auto start=i++;while(i<s.size()) {if(s[i++]=='"') break; if(s[i-1]=='\\') ++i;}return QJsonDocument::fromJson(QByteArray::fromStdString("["+s.substr(start,i-start)+"]")).array()[0].toString();}
    void value(unsigned depth) {
        if(depth>128) throw Error(3,"JSON nesting limit exceeded");
        ws();
        if(s[i]=='{') {++i;ws();std::set<QString> keys;if(s[i]=='}') {++i;return;} do {ws();auto key=string();if(!keys.insert(key).second) throw Error(3,"Duplicate JSON member: "+key.toStdString());ws();++i;value(depth+1);ws();if(s[i]=='}') {++i;return;}++i;} while(true);}
        if(s[i]=='[') {++i;ws();if(s[i]==']') {++i;return;}do {value(depth+1);ws();if(s[i]==']') {++i;return;}++i;}while(true);}
        if(s[i]=='"') {(void)string();return;}while(i<s.size() && s[i]!=',' && s[i]!=']' && s[i]!='}' && s[i]!=' ' && s[i]!='\n' && s[i]!='\r' && s[i]!='\t') ++i;
    }
public: explicit DuplicateKeys(const std::string& text):s(text){} void check(){value(0);}
};
}
QJsonObject strictObject(const std::string& s) {
    QJsonParseError error;auto d=QJsonDocument::fromJson(QByteArray::fromStdString(s),&error);
    if(error.error!=QJsonParseError::NoError || !d.isObject()) throw Error(3,"Malformed JSON object");
    DuplicateKeys(s).check();return d.object();
}
QString stageId(processing::StageId id) {
    switch(id) {case processing::StageId::Normalize:return "normalize";case processing::StageId::WindowLevel:return "window_level";case processing::StageId::BrightnessContrast:return "brightness_contrast";case processing::StageId::Gamma:return "gamma";case processing::StageId::Clahe:return "clahe";case processing::StageId::Denoise:return "denoise";case processing::StageId::Sharpen:return "sharpen";case processing::StageId::Invert:return "invert";}
    throw Error(3,"Unknown stage ID");
}
QJsonObject parametersJson(const processing::StageDefinition& stage) {
    using namespace processing;
    return std::visit([&](const auto& p)->QJsonObject {
        using T=std::decay_t<decltype(p)>;
        StageId expected;QJsonObject o;
        if constexpr(std::is_same_v<T,NormalizationParameters>) expected=StageId::Normalize;
        else if constexpr(std::is_same_v<T,WindowLevelParameters>) {expected=StageId::WindowLevel;o={{"window",p.window},{"level",p.level}};}
        else if constexpr(std::is_same_v<T,BrightnessContrastParameters>) {expected=StageId::BrightnessContrast;o={{"brightness",p.brightness},{"contrast",p.contrast}};}
        else if constexpr(std::is_same_v<T,GammaParameters>) {expected=StageId::Gamma;o={{"gamma",p.gamma}};}
        else if constexpr(std::is_same_v<T,ClaheParameters>) {expected=StageId::Clahe;o={{"clipLimit",p.clipLimit},{"tileGridSize",integer(p.tileGridSize)}};}
        else if constexpr(std::is_same_v<T,DenoiseParameters>) {expected=StageId::Denoise;if(p.mode!=DenoiseMode::Gaussian && p.mode!=DenoiseMode::Median) throw Error(3,"Unknown denoise mode");o={{"mode",p.mode==DenoiseMode::Gaussian?"gaussian":"median"},{"kernelSize",integer(p.kernelSize)},{"sigma",p.sigma}};}
        else if constexpr(std::is_same_v<T,SharpenParameters>) {expected=StageId::Sharpen;o={{"amount",p.amount},{"radius",p.radius},{"threshold",p.threshold}};}
        else expected=StageId::Invert;
        if(stage.id!=expected) throw Error(3,"Stage ID/parameter variant mismatch");
        for(const auto& v:o) if(v.isNull()) throw Error(3,"Nonfinite pipeline parameter");
        return o;
    },stage.parameters);
}
QJsonObject pipelineJson(const processing::PipelineDefinition& p) {
    QJsonArray stages;for(const auto& s:p.stages) stages.append(QJsonObject{{"id",stageId(s.id)},{"enabled",s.enabled},{"parameters",parametersJson(s)}});
    return {{"schemaVersion",integer(p.version.schemaVersion)},{"orderVersion",integer(p.version.orderVersion)},{"configurationRevision",integer(p.version.configurationRevision)},{"stages",stages}};
}
std::string serializePipeline(const processing::PipelineDefinition& p) {return json(pipelineJson(p));}
QString utcNow() {return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);}
QJsonObject dimensions(std::uint32_t w,std::uint32_t h) {return {{"width",integer(w)},{"height",integer(h)}};}
QJsonObject orientationJson(core::Orientation o) {
    QString r;switch(o.rotation) {case core::Rotation::Degrees0:r="degrees0";break;case core::Rotation::Degrees90:r="degrees90";break;case core::Rotation::Degrees180:r="degrees180";break;case core::Rotation::Degrees270:r="degrees270";break;default:throw Error(3,"Invalid orientation");}
    return {{"flipHorizontal",o.flipHorizontal},{"flipVertical",o.flipVertical},{"rotation",r}};
}
QJsonObject descriptorJson(const core::SourcePixelFormat& f,const core::ImageLayout& l) {
    return {{"canonicalName",qs(f.canonicalName)},{"canonicalEncoding",integer(f.canonicalEncoding)},{"validBits",integer(f.validBits)},{"sampleMaximum",integer(f.sampleMaximum)},{"packing",f.packing==core::SourcePacking::Unpacked?"unpacked":"packed"},{"alignment",f.alignment==core::BitAlignment::LeastSignificant?"least_significant":"most_significant"},{"applicationStorage",f.applicationStorage==core::StorageType::UInt16?"uint16":"uint8"},{"nativeDimensions",dimensions(l.width(),l.height())},{"strideBytes",integer(l.strideBytes())},{"payloadBytes",integer(l.payloadBytes())}};
}
const std::vector<std::string>& rowIds() {static const std::vector<std::string> ids={"normalize","window_level","brightness_contrast","gamma","clahe","denoise_gaussian","denoise_median","sharpen","invert","full_standard_identity","full_standard_nonidentity"};return ids;}
QJsonObject payloadJson(const std::filesystem::path& dir,const std::string& file,const Image& im,bool gray8) {
    const auto bytes=readFile(dir/file);if(decodePgm(bytes)!=im) throw Error(3,"Written PGM differs from result");
    return {{"file",qs(file)},{"format","pgm_p5_u16"},{"width",integer(im.width)},{"height",integer(im.height)},{"maxValue",65535},{"byteOrder","big_endian"},{"payloadEncoding",gray8?"gray8_zero_extended_to_u16_big_endian":"u16_big_endian"},{"sha256",qs(sha256(bytes))}};
}
void ensureCandidateDirectory(const std::filesystem::path& requested) {
    const auto path=std::filesystem::weakly_canonical(requested),root=std::filesystem::weakly_canonical(LUMORA_REFERENCE_ROOT);
    auto sameComponent=[](const std::filesystem::path& a,const std::filesystem::path& b) {
#if defined(_WIN32)
        return QString::fromStdU16String(a.u16string()).compare(QString::fromStdU16String(b.u16string()),Qt::CaseInsensitive)==0;
#else
        return a==b;
#endif
    };
    auto p=path.begin(),r=root.begin();for(;p!=path.end() && r!=root.end() && sameComponent(*p,*r);++p,++r) {}
    if(r==root.end()) throw Error(2,"Candidate output cannot be inside committed references");
    if(std::filesystem::exists(path) && (!std::filesystem::is_directory(path) || !std::filesystem::is_empty(path))) throw Error(2,"Candidate output must be a new or empty directory");
    std::filesystem::create_directories(path);
}
}
namespace lumora::evidence {
QJsonObject sessionResources(const processing::ProcessingResources& r) {
    return {{"scope","prepared_session"},{"limitScope","session_accounted_storage"},{"limitBytes",integer(r.storageBudgetBytes)},{"requiredBytes",integer(r.requiredStorageBytes)},{"fixedBytes",integer(r.fixedStorageBytes)},{"threeOwnerReserveBytes",integer(r.activationReserveBytes)},{"gammaCacheReserveBytes",integer(r.gammaCacheReserveBytes)},{"candidateRequiredBytes",integer(r.candidateRequiredBytes)},
        {"boundedStatistics",QJsonObject{{"externalSessionBytes",integer(r.externalSessionBytes)},{"processingPoolBytes",integer(r.processingPoolBytes)},{"displayPoolBytes",integer(r.displayPoolBytes)},{"frameObjectBytes",integer(r.frameObjectBytes)},{"orientationBytes",integer(r.orientationBytes)},{"engineStateBytes",integer(r.engineStateBytes)},{"activationEnvelopeBytes",integer(r.activationEnvelopeBytes)},{"actualRetainedStageBytes",integer(r.actualRetainedStageBytes)}}},
        {"exclusions",QJsonArray{"allocator_headers","BufferPool_two_and_FrameObjectPool_three_STL_owner_controls","compiler_probe_temporaries","retained_error_diagnostic_strings","earlier_session_frames","RawFrame_metadata_and_shared_owner_controls","tool_input_samples_JSON_retention_slot_sentinel_owner_storage"}}};
}
QJsonObject standaloneResources(std::size_t scratch,std::size_t fixed,std::size_t imageBytes) {
    return {{"scope","standalone_stage"},{"limitScope","standalone_scratch_only"},{"limitBytes",integer(256U*1024U*1024U)},{"requiredBytes",integer(scratch+fixed)},{"fixedBytes",QJsonValue()},{"threeOwnerReserveBytes",QJsonValue()},{"gammaCacheReserveBytes",QJsonValue()},{"candidateRequiredBytes",QJsonValue()},
        {"boundedStatistics",QJsonObject{{"scratchBytes",integer(scratch)},{"stageOwnerBytes",integer(fixed)},{"separateHarnessImageBytes",integer(imageBytes)}}},{"exclusions",QJsonArray{"allocator_headers","standalone_source_destination_payloads_reported_separately","caller_pattern_image_configuration_samples_JSON_and_facade_storage"}}};
}
}
