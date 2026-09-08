#include <cstdlib>
#if defined(_WIN32)
#define CONTROL_EXPORT __declspec(dllexport)
#else
#define CONTROL_EXPORT __attribute__((visibility("default")))
#endif
extern "C" CONTROL_EXPORT bool lumora_heap_shared_control() {
    void* (*volatile allocate)(std::size_t)=std::malloc;
    void (*volatile release)(void*)=std::free;
    auto* p=static_cast<unsigned char*>(allocate(67));if(!p) return false;
    p[0]=23;p[66]=41;const bool valid=p[0]==23 && p[66]==41;release(p);return valid;
}
