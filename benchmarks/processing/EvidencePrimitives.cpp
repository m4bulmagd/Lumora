#include "EvidenceSupport.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <sstream>
namespace lumora::evidence {
namespace {
std::uint64_t number(std::string_view s,int base=10) {
    std::uint64_t n{}; const auto r=std::from_chars(s.data(),s.data()+s.size(),n,base);
    if(s.empty() || r.ec!=std::errc{} || r.ptr!=s.data()+s.size()) throw Error(2,"Invalid unsigned integer");
    return n;
}
std::uint32_t count(const std::string& s) { auto n=number(s); if(n==0 || n>UINT32_MAX) throw Error(2,"Count outside representable range"); return static_cast<std::uint32_t>(n); }
std::size_t pixels(std::uint32_t w,std::uint32_t h) {
    if(!w || !h || static_cast<std::uint64_t>(w)*h>std::numeric_limits<std::size_t>::max()/2U) throw Error(3,"Invalid image extent");
    return static_cast<std::size_t>(w)*h;
}
void add(std::uint64_t& value,std::uint64_t n) { if(n>UINT64_MAX-value) throw Error(5,"Trace counter overflow"); value+=n; }
}
Options parseOptions(Tool tool,std::span<const std::string> args) {
    Options o; if(tool==Tool::Allocation) o.measured=1000;
    if(args.size()==1 && args[0]=="--help") { o.help=true; return o; }
    std::set<std::string> seen;
    for(std::size_t i=0;i<args.size();++i) {
        const auto& key=args[i]; if(!seen.insert(key).second) throw Error(2,"Duplicate option: "+key);
        if(key=="--smoke") {o.smoke=true; continue;}
        const bool workload=key=="--sizes" || key=="--warm-up" || key=="--measured";
        const bool sourceFormat=key=="--source-format";
        if(key!="--output" && !(tool==Tool::Benchmark && workload) && !(tool==Tool::Allocation && key=="--heap-trace") && !((tool==Tool::Benchmark || tool==Tool::Allocation) && sourceFormat)) throw Error(2,"Unknown option: "+key);
        if(++i==args.size() || args[i].empty() || args[i].starts_with("--")) throw Error(2,"Missing option value: "+key);
        const auto& value=args[i];
        if(key=="--output") o.output=std::filesystem::path(std::u8string(value.begin(),value.end()));
        else if(key=="--heap-trace") o.heapTrace=std::filesystem::path(std::u8string(value.begin(),value.end()));
        else if(key=="--source-format") {
            if(value=="mono16") o.sourceFormat=SourceFormat::Mono16;
            else if(value=="mono12") o.sourceFormat=SourceFormat::Mono12;
            else throw Error(2,"Unknown source format: "+value);
        }
        else if(key=="--warm-up") o.warmUp=count(value);
        else if(key=="--measured") o.measured=count(value);
        else {
            o.sizes.clear(); std::size_t start=0;
            do { const auto end=value.find(',',start); const auto s=count(value.substr(start,end==std::string::npos?end:end-start));
                // Checked engine admission below handles budgets without allocating images.
                if(s<8 || s>static_cast<std::uint32_t>(INT32_MAX) || (!o.sizes.empty() && s<=o.sizes.back())) throw Error(2,"Sizes must be ascending unique preparable extents");
                o.sizes.push_back(s); if(end==std::string::npos) break; start=end+1;
            } while(true);
        }
    }
    if(o.output.empty()) throw Error(2,"--output is required");
    const auto work=seen.count("--sizes")+seen.count("--warm-up")+seen.count("--measured");
    if((work!=0 && work!=3) || (o.smoke && work)) throw Error(2,"Specify all three workload options, or --smoke");
    if(o.smoke) {o.workload="smoke";o.sizes={64,128};o.warmUp=2;o.measured=tool==Tool::Allocation?20:5;}
    else if(work) o.workload="custom";
    return o;
}
Image makePattern(const Pattern& p) {
    Image out{p.width,p.height,std::vector<std::uint16_t>(pixels(p.width,p.height))}; std::uint32_t state=p.seed;
    if(p.maximum>65535) throw Error(3,"Pattern maximum outside U16");
    for(std::uint32_t y=0;y<p.height;++y) for(std::uint32_t x=0;x<p.width;++x) {
        const std::size_t i=static_cast<std::size_t>(y)*p.width+x; std::uint64_t v=0;
        if(p.id=="ramp_u16_v1") v=out.pixels.size()==1?0:static_cast<std::uint64_t>(i)*p.maximum/(out.pixels.size()-1);
        else if(p.id=="gradient_xy_u16_v1") v=(p.width==1?0:(static_cast<std::uint64_t>(x)*65535/(p.width-1))/2)+(p.height==1?0:(static_cast<std::uint64_t>(y)*65535/(p.height-1))/2);
        else if(p.id=="step_edges_u16_v1") v=((x<p.width/2)!=(y<p.height/2))?61440:4096;
        else if(p.id=="xorshift32_u16_v1") {state^=state<<13;state^=state>>17;state^=state<<5;v=state>>16;}
        else if(p.id=="orientation_asymmetric_u16_v1") {if(p.width!=7 || p.height!=5) throw Error(3,"Asymmetric pattern is fixed 7x5");v=((x+1)+257*(y+1)+4096*((3*x+y)%13))&0xffffU;}
        else throw Error(3,"Unknown pattern");
        out.pixels[i]=static_cast<std::uint16_t>(v);
    } return out;
}
Image makeMeasurementInput(std::uint32_t width,std::uint32_t height,SourceFormat sourceFormat) {
    auto image=makePattern({"xorshift32_u16_v1",width,height});
    if(sourceFormat==SourceFormat::Mono12) for(auto& value:image.pixels) value=static_cast<std::uint16_t>(value>>4U);
    return image;
}
std::string encodePgm(const Image& in) {
    if(in.pixels.size()!=pixels(in.width,in.height)) throw Error(3,"PGM extent/payload mismatch");
    std::string out="P5\n"+std::to_string(in.width)+" "+std::to_string(in.height)+"\n65535\n";
    out.reserve(out.size()+in.pixels.size()*2);
    for(auto v:in.pixels) {out.push_back(static_cast<char>(v>>8));out.push_back(static_cast<char>(v&255));} return out;
}
Image decodePgm(const std::string& bytes) {
    try {
        if(!bytes.starts_with("P5\n")) throw Error(3,"PGM must use exact P5 header");
        const auto space=bytes.find(' ',3),line=bytes.find('\n',3);
        if(space==std::string::npos || line==std::string::npos || space>=line) throw Error(3,"Malformed PGM dimensions");
        const auto w=count(bytes.substr(3,space-3)),h=count(bytes.substr(space+1,line-space-1));
        if(bytes.substr(line+1,6)!="65535\n") throw Error(3,"PGM maximum must be 65535");
        const auto n=pixels(w,h),start=line+7;
        if(bytes.size()-start!=n*2) throw Error(3,"PGM payload length mismatch");
        Image out{w,h,std::vector<std::uint16_t>(n)};
        for(std::size_t i=0;i<n;++i) out.pixels[i]=static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[start+2*i])<<8)|static_cast<unsigned char>(bytes[start+2*i+1]));
        return out;
    } catch(const Error& e) {throw Error(3,e.what());}
}
std::string readFile(const std::filesystem::path& path) {
    QFile f(QString::fromStdU16String(path.u16string())); if(!f.open(QIODevice::ReadOnly)) throw Error(3,"Cannot read "+path.string()); return f.readAll().toStdString();
}
void atomicWrite(const std::filesystem::path& path,const std::string& value) {
    QSaveFile f(QString::fromStdU16String(path.u16string()));
    if(!f.open(QIODevice::WriteOnly) || f.write(value.data(),static_cast<qint64>(value.size()))!=static_cast<qint64>(value.size()) || !f.commit()) throw Error(3,"Cannot atomically write "+path.string());
}
std::string sha256(const std::string& s) {return QCryptographicHash::hash(QByteArrayView(s.data(),static_cast<qsizetype>(s.size())),QCryptographicHash::Sha256).toHex().toStdString();}
Statistics statistics(std::span<const std::uint64_t> samples,std::uint64_t wall) {
    if(samples.empty() || !wall || std::find(samples.begin(),samples.end(),0)!=samples.end()) throw Error(3,"Positive timing samples and wall duration required");
    std::vector<std::uint64_t> sorted(samples.begin(),samples.end());std::sort(sorted.begin(),sorted.end()); const auto n=sorted.size();
    const auto lo=sorted[(n-1)/2],hi=sorted[n/2]; const double median=static_cast<double>(lo)+static_cast<double>(hi-lo)/2.0;
    const auto rank=n-n/20;return {median,sorted[rank-1],wall,static_cast<double>(n)*1e9/static_cast<double>(wall)};
}
TraceCounts parseTrace(const std::string& input) {
    std::istringstream stream(input);std::string line;TraceCounts result;
    if(!std::getline(stream,line) || line!="= Start") throw Error(5,"Missing trace start");
    bool ended=false,pendingRealloc=false;
    while(std::getline(stream,line)) {
        if(ended) throw Error(5,"Content after trace end");
        if(line=="= End") {if(pendingRealloc) throw Error(5,"Unpaired realloc");ended=true;continue;}
        std::istringstream tokens(line);std::string at,caller,op,pointer,size,extra;
        if(!(tokens>>at>>caller>>op>>pointer) || at!="@" || op.size()!=1) throw Error(5,"Unknown trace record");
        auto pointerValid=[](const std::string& p) {if(p=="(nil)") return; if(!p.starts_with("0x")) throw Error(5,"Invalid trace pointer"); (void)number(std::string_view(p).substr(2),16);};
        pointerValid(pointer); const char event=op[0]; std::uint64_t bytes=0;
        if(event=='+' || event=='>' || event=='!') {if(!(tokens>>size) || !size.starts_with("0x")) throw Error(5,"Missing trace size");bytes=number(std::string_view(size).substr(2),16);}
        if(tokens>>extra) throw Error(5,"Unexpected trace fields");
        if(pendingRealloc && event!='>') throw Error(5,"Unpaired realloc transition");
        add(result.events,1);
        switch(event) {
        case '+': if(pointer=="(nil)" || pointer=="0x0") add(result.allocationFailures,1); else {add(result.allocations,1);add(result.reportedBytes,bytes);} break;
        case '-': add(result.releases,1);break;
        case '<': if(pointer=="(nil)") throw Error(5,"Null realloc old");add(result.reallocOld,1);pendingRealloc=true;break;
        case '>': if(!pendingRealloc || pointer=="(nil)") throw Error(5,"Invalid realloc new");add(result.reallocNew,1);add(result.reportedBytes,bytes);pendingRealloc=false;break;
        case '!':add(result.reallocFailures,1);break;
        default:throw Error(5,"Unknown allocator event");
        }
    }
    if(!ended || input.empty() || input.back()!='\n') throw Error(5,"Missing/truncated trace end");
    return result;
}
}
