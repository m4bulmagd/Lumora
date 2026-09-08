#pragma once
#include "EvidenceJson.hpp"
namespace lumora::evidence {
class HeapTrace {
    std::filesystem::path directory_,active_;
    QJsonObject capability_;
    bool armed_=false;
public:
    explicit HeapTrace(const std::filesystem::path&);
    ~HeapTrace();
    void start(std::string_view row);
    void stop() noexcept;
    QJsonObject result(std::uint32_t cycles,bool smoke);
};
}
