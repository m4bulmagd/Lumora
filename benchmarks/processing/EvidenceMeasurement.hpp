#pragma once
#include "EvidenceWorkload.hpp"
#include "AllocationTracker.hpp"
namespace lumora::evidence {
test::AllocationCounts allocationControls();
QJsonObject helperControlJson(HelperAllocationControlResult,test::AllocationCounts,QJsonValue trace={});
QJsonObject allocationJson(test::AllocationCounts,const QString& region);
QJsonObject measureBenchmarkRow(const std::string&,std::uint32_t,const Options&);
void writeProgress(const std::filesystem::path&,QJsonObject&,bool complete=false);
}
