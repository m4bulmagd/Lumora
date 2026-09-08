#pragma once
#include <lumora/processing/ProcessingDefaults.hpp>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace lumora::evidence {
struct Error : std::runtime_error { int exitCode; Error(int code,const std::string& message):std::runtime_error(message),exitCode(code){} };
enum class Tool { Benchmark, Generator, Allocation };
struct Options { bool help=false,smoke=false; std::filesystem::path output,heapTrace; std::string workload="standard"; std::vector<std::uint32_t> sizes{512,1024,2048}; std::uint32_t warmUp=100,measured=500; };
Options parseOptions(Tool,std::span<const std::string>);
struct Image { std::uint32_t width,height; std::vector<std::uint16_t> pixels; bool operator==(const Image&) const=default; };
struct Pattern { std::string id; std::uint32_t width,height,maximum=65535; std::uint32_t seed=0x6D2B79F5U; };
Image makePattern(const Pattern&);
std::string encodePgm(const Image&);
Image decodePgm(const std::string&);
std::string readFile(const std::filesystem::path&);
void atomicWrite(const std::filesystem::path&,const std::string&);
std::string sha256(const std::string&);
struct Statistics { double median; std::uint64_t p95,wall; double fps; };
Statistics statistics(std::span<const std::uint64_t>,std::uint64_t wall);
struct TraceCounts { std::uint64_t events=0,allocations=0,allocationFailures=0,releases=0,reallocOld=0,reallocNew=0,reallocFailures=0,reportedBytes=0; };
TraceCounts parseTrace(const std::string&);
// Strict owned-artifact validation, including reference payloads relative to base.
void validateArtifact(const std::string&,const std::filesystem::path& base={});
std::string serializePipeline(const processing::PipelineDefinition&);
void ensureCandidateDirectory(const std::filesystem::path&);
}
