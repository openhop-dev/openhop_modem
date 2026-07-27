#include "../include/ota_staged_writer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

using namespace OtaStagedWriter;

namespace {

// Independent compact test-side SHA-256 used only to construct package fixtures.
class TestSha256 {
public:
    TestSha256() { reset(); }
    void reset() {
        static const uint32_t init[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                         0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        std::memcpy(h, init, sizeof(h)); used = 0; bits = 0;
    }
    void update(const uint8_t* p, size_t n) {
        bits += static_cast<uint64_t>(n) * 8;
        while (n) { size_t take = std::min(n, sizeof(block)-used); std::memcpy(block+used,p,take); used+=take;p+=take;n-=take; if(used==64){transform();used=0;} }
    }
    std::array<uint8_t,32> finish() {
        block[used++]=0x80; if(used>56){while(used<64)block[used++]=0;transform();used=0;} while(used<56)block[used++]=0;
        for(int i=7;i>=0;--i) block[used++]=static_cast<uint8_t>(bits>>(i*8));
        transform();
        std::array<uint8_t,32> out{}; for(size_t i=0;i<8;++i) for(size_t j=0;j<4;++j) out[i*4+j]=static_cast<uint8_t>(h[i]>>(24-j*8)); return out;
    }
private:
    static uint32_t rotr(uint32_t x,uint32_t n){return (x>>n)|(x<<(32-n));}
    void transform(){
        static const uint32_t k[64]={
          0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
          0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
          0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
          0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
          0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
          0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
          0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
          0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64]; for(size_t i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(size_t i=16;i<64;++i){uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(size_t i=0;i<64;++i){uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25),ch=(e&f)^((~e)&g),t1=z+s1+ch+k[i]+w[i],s0=rotr(a,2)^rotr(a,13)^rotr(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    uint32_t h[8]; uint8_t block[64]{}; size_t used{}; uint64_t bits{};
};

void put16(std::vector<uint8_t>& p,size_t o,uint16_t v){p[o]=uint8_t(v);p[o+1]=uint8_t(v>>8);}
void put32(std::vector<uint8_t>& p,size_t o,uint32_t v){for(size_t i=0;i<4;++i)p[o+i]=uint8_t(v>>(8*i));}

std::vector<uint8_t> package(const std::vector<uint8_t>& payload) {
    const char target[]="rak4631_wismesh_eth", version[]="test-1";
    const size_t header=64+sizeof(target)-1+sizeof(version)-1;
    std::vector<uint8_t> out(header+payload.size(),0);
    const uint8_t magic[8]={0x4f,0x48,0x4f,0x54,0x41,0x0d,0x0a,0x1a}; std::memcpy(out.data(),magic,8);
    put16(out,8,1);put16(out,10,uint16_t(header));put16(out,12,sizeof(target)-1);put16(out,14,sizeof(version)-1);
    put16(out,16,0);put16(out,18,0);put32(out,20,0x26000);put32(out,24,0x00b6);put32(out,28,uint32_t(payload.size()));
    TestSha256 sha;sha.update(payload.data(),payload.size());auto digest=sha.finish();std::copy(digest.begin(),digest.end(),out.begin()+32);
    std::memcpy(out.data()+64,target,sizeof(target)-1);std::memcpy(out.data()+64+sizeof(target)-1,version,sizeof(version)-1);
    std::copy(payload.begin(),payload.end(),out.begin()+header); return out;
}

struct Op { char kind; uint32_t address; size_t length; };
class FakeFlash final : public Flash {
public:
    bool erasePage(uint32_t address) override { log.push_back({'e',address,PAGE_SIZE}); if(failErase--==0)return false; for(uint32_t i=0;i<PAGE_SIZE;++i)bytes.erase(address+i);return true; }
    bool write(uint32_t address,const uint8_t* data,size_t length) override { log.push_back({'w',address,length}); if(failWrite--==0)return false; for(size_t i=0;i<length;++i)bytes[address+uint32_t(i)]=data[i];return true; }
    bool read(uint32_t address,uint8_t* data,size_t length) override { log.push_back({'r',address,length}); if(failRead--==0)return false; for(size_t i=0;i<length;++i){auto it=bytes.find(address+uint32_t(i));data[i]=it==bytes.end()?0xff:it->second;}return true; }
    bool writeErasesPage() const override { return intrinsicErase; }
    std::map<uint32_t,uint8_t> bytes; std::vector<Op> log; int failErase=-1,failWrite=-1,failRead=-1; bool intrinsicErase=false;
};
class FakeHooks final : public ServiceHooks { public: void betweenFlashUnits() override { ++calls; } size_t calls=0; };

void pump(Writer& writer) { for(size_t guard=0;!writer.isTerminal()&&guard<1000000;++guard) writer.service(); assert(writer.isTerminal()); }
void submit(Writer& writer,const std::vector<uint8_t>& pkg,size_t fragment) {
    size_t offset=0;
    while(offset<pkg.size()&&!writer.isTerminal()) { size_t n=std::min(fragment,pkg.size()-offset), used=writer.push(pkg.data()+offset,n); offset+=used; if(used==0)writer.service(); }
    if(!writer.isTerminal())writer.finish();
    pump(writer);
}
void assertSafe(const FakeFlash& flash){for(const auto& op:flash.log){assert(op.address>=STAGING_START);assert(uint64_t(op.address)+op.length<=STAGING_END);}}

void test_one_byte_fragments_valid_write_readback_and_tail_padding(){
    std::vector<uint8_t> payload(515);for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i*37);
    FakeFlash flash;FakeHooks hooks;Writer writer(flash,hooks);submit(writer,package(payload),1);
    assert(writer.status()==Status::VERIFIED);assert(writer.receivedPayload()==payload.size());assertSafe(flash);
    for(size_t i=0;i<payload.size();++i)assert(flash.bytes[STAGING_START+i]==payload[i]);
    for(size_t i=payload.size();i<WRITE_CHUNK;++i)assert(flash.bytes[STAGING_START+i]==0xff);
    assert(hooks.calls==flash.log.size());
}

void test_sha256_matches_nist_abc_vector(){
    std::vector<uint8_t> payload{'a','b','c'};
    auto pkg=package(payload);
    const uint8_t expected[32]={
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    std::copy(std::begin(expected),std::end(expected),pkg.begin()+32);
    FakeFlash flash;FakeHooks hooks;Writer writer(flash,hooks);submit(writer,pkg,1);
    assert(writer.status()==Status::VERIFIED);
}

void test_backend_with_intrinsic_page_erase_skips_preliminary_erase(){
    FakeFlash flash;flash.intrinsicErase=true;FakeHooks hooks;Writer writer(flash,hooks);
    submit(writer,package({1,2,3,4,5}),3);
    assert(writer.status()==Status::VERIFIED);
    for(const auto& op:flash.log)assert(op.kind!='e');
}

void test_max_boundary_sparse_fake(){
    std::vector<uint8_t> payload(PAYLOAD_MAX);for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i);
    FakeFlash flash;FakeHooks hooks;Writer writer(flash,hooks);submit(writer,package(payload),997);
    assert(writer.status()==Status::VERIFIED);assertSafe(flash);
    size_t erases=0;for(const auto& op:flash.log)if(op.kind=='e')++erases;assert(erases==PAYLOAD_MAX/PAGE_SIZE);
}

void expectHeaderFailure(std::vector<uint8_t> pkg,Status expected){FakeFlash f;FakeHooks h;Writer w(f,h);submit(w,pkg,1);assert(w.status()==expected);assert(f.log.empty());}
void test_all_malformed_fields_fail_before_erase(){
    auto base=package({1,2,3,4});
    auto mutate=[&](size_t o,uint8_t v,Status s){auto p=base;p[o]=v;expectHeaderFailure(p,s);};
    mutate(0,0,Status::BAD_MAGIC);mutate(8,2,Status::BAD_SCHEMA);mutate(10,0,Status::BAD_HEADER_LENGTH);
    mutate(12,0,Status::BAD_TARGET_LENGTH);mutate(14,0,Status::BAD_VERSION_LENGTH);mutate(16,1,Status::BAD_SIGNATURE);
    mutate(18,1,Status::BAD_SIGNATURE);mutate(20,1,Status::WRONG_ORIGIN);mutate(24,1,Status::WRONG_FWID);
    auto empty=base;put32(empty,28,0);expectHeaderFailure(empty,Status::EMPTY_PAYLOAD);
    auto over=base;put32(over,28,PAYLOAD_MAX+1);expectHeaderFailure(over,Status::PAYLOAD_TOO_LARGE);
    auto target=base;target[64]^=1;expectHeaderFailure(target,Status::WRONG_TARGET);
    auto nul=base;nul[64+20]=0;expectHeaderFailure(nul,Status::BAD_VERSION);
}

void test_empty_truncated_trailing_and_hash(){
    FakeFlash f;FakeHooks h;Writer empty(f,h);empty.finish();assert(empty.status()==Status::EMPTY_INPUT);assert(f.log.empty());
    auto p=package({1,2,3,4,5});p.pop_back();Writer truncated(f,h);submit(truncated,p,3);assert(truncated.status()==Status::TRUNCATED);
    auto trailing=package({1,2});trailing.push_back(9);Writer t(f,h);submit(t,trailing,999);assert(t.status()==Status::TRAILING_DATA);
    auto bad=package({1,2,3});bad.back()^=1;FakeFlash f2;Writer hash(f2,h);submit(hash,bad,7);assert(hash.status()==Status::HASH_MISMATCH);assertSafe(f2);
}

void test_injected_failures_and_readback_corruption(){
    for(char kind:std::array<char,3>{'e','w','r'}){FakeFlash f;FakeHooks h;if(kind=='e')f.failErase=0;if(kind=='w')f.failWrite=0;if(kind=='r')f.failRead=0;Writer w(f,h);submit(w,package({1,2,3,4,5}),2);assert(w.status()==(kind=='e'?Status::ERASE_FAILED:kind=='w'?Status::WRITE_FAILED:Status::READ_FAILED));assertSafe(f);}
    FakeFlash f;FakeHooks h;Writer w(f,h);auto p=package({4,5,6,7});size_t off=0;while(off<p.size()){size_t used=w.push(p.data()+off,p.size()-off);off+=used;if(!used)w.service();}w.finish();
    while(w.status()!=Status::READING_BACK&&!w.isTerminal())w.service();
    f.bytes[STAGING_START]^=1;
    pump(w);
    assert(w.status()==Status::READBACK_MISMATCH);
}

void test_cancel_and_restart_never_preserve_verified(){
    FakeFlash f;FakeHooks h;Writer first(f,h);auto p=package(std::vector<uint8_t>(9000,0x5a));size_t off=0;while(off<100){size_t used=first.push(p.data()+off,100-off);off+=used;if(!used)first.service();}first.cancel();assert(first.status()==Status::CANCELED);assert(!first.verified());
    Writer restarted(f,h);assert(restarted.status()==Status::RECEIVING_HEADER);assert(!restarted.verified());restarted.finish();assert(restarted.status()==Status::EMPTY_INPUT);
}

} // namespace
int main(){
 test_one_byte_fragments_valid_write_readback_and_tail_padding();test_sha256_matches_nist_abc_vector();test_backend_with_intrinsic_page_erase_skips_preliminary_erase();test_max_boundary_sparse_fake();test_all_malformed_fields_fail_before_erase();
 test_empty_truncated_trailing_and_hash();test_injected_failures_and_readback_corruption();test_cancel_and_restart_never_preserve_verified();
 std::cout<<"ota_staged_writer_test: OK\n";
}
