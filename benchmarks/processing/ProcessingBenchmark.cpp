#include "EvidenceMeasurement.hpp"
#include <QCoreApplication>
#include <QtCore/qtenvironmentvariables.h>
#include <cstdio>
int main(int argc,char** argv) {
    using namespace lumora::evidence;QCoreApplication application(argc,argv);QJsonObject root;std::filesystem::path output;
    try {
        std::vector<std::string> args;for(const auto& a:application.arguments().mid(1)) args.push_back(a.toStdString());const auto o=parseOptions(Tool::Benchmark,args);
        if(o.help) {std::puts("lumora_processing_benchmark --output FILE [--source-format mono16|mono12] [--smoke | --sizes 512,1024,2048 --warm-up 100 --measured 500]");return 0;}
        if(!qEnvironmentVariableIsEmpty("LD_PRELOAD")) throw Error(5,"Ordinary benchmark requires no LD_PRELOAD instrumentation");
        for(auto size:o.sizes) for(bool oriented:{false,true}) {try {if(!Session::assess(size,size,{oriented,false,oriented?lumora::core::Rotation::Degrees90:lumora::core::Rotation::Degrees0}).plan) throw Error(2,"Requested size exceeds preparable Standard session budget");}catch(const Error& e){throw Error(2,e.what());}}
        (void)allocationControls();output=o.output;QJsonArray sizes;for(auto s:o.sizes) sizes.append(integer(s));
        root={{"schemaVersion",o.sourceFormat==SourceFormat::Mono12?3:2},{"artifactType","lumora.processing.benchmark"},{"runStatus","incomplete"},{"complete",false},{"workload",QJsonObject{{"kind",qs(o.workload)},{"requestedSizes",sizes},{"warmUpFrames",integer(o.warmUp)},{"measuredFrames",integer(o.measured)}}},{"generatedUtc",utcNow()},{"rows",QJsonArray{}},{"failure",QJsonValue()}};
        if(o.sourceFormat==SourceFormat::Mono12) root["sourceFormat"]="mono12";
        writeProgress(output,root);for(auto size:o.sizes) for(const auto& id:measurementRowIds(o.sourceFormat)) {auto rows=root["rows"].toArray();rows.append(measureBenchmarkRow(id,size,o));root["rows"]=rows;writeProgress(output,root);}
        writeProgress(output,root,true);return 0;
    } catch(const Error& e) {if(!root.isEmpty()) {root["failure"]=QJsonObject{{"code",QString::number(e.exitCode)},{"message",e.what()},{"completedRows",root["rows"].toArray().size()}};try {writeProgress(output,root);}catch(...) {}}std::fprintf(stderr,"%s\n",e.what());return e.exitCode;}
    catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 3;}
}
