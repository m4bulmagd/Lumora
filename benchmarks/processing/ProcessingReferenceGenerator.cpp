#include "EvidenceWorkload.hpp"
#include <QCoreApplication>
#include <cstdio>
int main(int argc,char** argv) {
    using namespace lumora::evidence;QCoreApplication app(argc,argv);QJsonObject root;std::filesystem::path directory;
    try {
        std::vector<std::string> args;for(const auto& a:app.arguments().mid(1)) args.push_back(a.toStdString());const auto o=parseOptions(Tool::Generator,args);
        if(o.help) {std::puts("lumora_processing_reference_generator --output DIRECTORY [--smoke]; always candidate, refuses committed references/nonempty directories");return 0;}
        ensureCandidateDirectory(o.output);directory=o.output;
        root={{"schemaVersion",1},{"artifactType","lumora.processing.reference-candidate"},{"status","candidate"},{"workloadKind",o.smoke?"smoke":"candidate"},{"complete",false},{"generatedUtc",utcNow()},{"provenance",provenance()},{"pipelineDefinition",pipelineJson(lumora::processing::standardPipeline())},{"cases",QJsonArray{}},{"failure",QJsonValue()}};
        atomicWrite(directory/"manifest.json",json(root));
        for(const auto& c:referenceCases(o.smoke)) {
            const auto input=makePattern(c.pattern),candidate=executeCase(c,input);auto row=caseMetadata(c);
            const auto sf=c.id+"-source.pgm",cf=c.id+"-candidate.pgm";atomicWrite(directory/sf,encodePgm(input));atomicWrite(directory/cf,encodePgm(candidate));
            row["source"]=payloadJson(directory,sf,input);row["candidate"]=payloadJson(directory,cf,candidate,c.gray8);
            QJsonObject comparison{{"maxAbsoluteU16Error",QJsonValue()},{"changedPixelFraction",QJsonValue()},{"meanAbsoluteError",QJsonValue()}};
            const auto expectedPath=std::filesystem::path(LUMORA_REFERENCE_FIXTURES)/(o.smoke?"smoke":"ordinary")/(c.id+"-expected.pgm");
            if(std::filesystem::exists(expectedPath)) {auto expected=decodePgm(readFile(expectedPath));if(expected.width!=candidate.width || expected.height!=candidate.height) throw Error(3,"Reference comparison dimensions differ");std::uint64_t total=0,changed=0;std::uint32_t maximum=0;for(std::size_t i=0;i<expected.pixels.size();++i) {const auto delta=static_cast<std::uint32_t>(std::abs(static_cast<int>(expected.pixels[i])-candidate.pixels[i]));maximum=std::max(maximum,delta);total+=delta;changed+=delta!=0;}comparison={{"maxAbsoluteU16Error",integer(maximum)},{"changedPixelFraction",static_cast<double>(changed)/static_cast<double>(candidate.pixels.size())},{"meanAbsoluteError",static_cast<double>(total)/static_cast<double>(candidate.pixels.size())}};}
            row["comparison"]=comparison;row["acceptance"]=QJsonObject{{"status",c.exact?"candidate":"pending_review"},{"thresholds",QJsonValue()},{"review",QJsonValue()}};auto cases=root["cases"].toArray();cases.append(row);root["cases"]=cases;validateArtifact(json(root),directory);atomicWrite(directory/"manifest.json",json(root));
        }
        root["complete"]=true;validateArtifact(json(root),directory);atomicWrite(directory/"manifest.json",json(root));return 0;
    } catch(const Error& e) {if(!root.isEmpty()) {root["complete"]=false;root["failure"]=QJsonObject{{"code",QString::number(e.exitCode)},{"message",e.what()},{"completedRows",root["cases"].toArray().size()}};try {atomicWrite(directory/"manifest.json",json(root));}catch(...) {}}std::fprintf(stderr,"%s\n",e.what());return e.exitCode;}catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 3;}
}
