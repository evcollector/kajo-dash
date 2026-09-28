#include "ride_replay_core.h"
#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
using namespace ride_replay;
static void check(bool ok,const char *message) { if(!ok){std::cerr<<message<<'\n';std::exit(1);} }
static void put16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(8*i);}
struct Fixture {
  std::vector<uint8_t> bytes; unsigned reads=0; bool fail=false;
  Fixture(){bytes.resize(kHeaderBytes);auto *p=bytes.data();memcpy(p,"KAJL",4);put16(p+4,kLogVersion);put16(p+6,kHeaderBytes);put16(p+8,kRecordBytes);p[10]=5;put32(p+12,12);headerCrc();}
  void headerCrc(){put16(bytes.data()+30,crc16(bytes.data(),30));}
  void add(uint32_t t,int watts=210,uint32_t mask=7){size_t n=bytes.size();bytes.resize(n+kRecordBytes);auto*p=bytes.data()+n;put32(p,t);put32(p+8,watts);put16(p+24,250);put16(p+26,3980);put32(p+38,mask);put16(p+42,crc16(p,42));}
  // Every chartable field plus the trip counter, as the logger writes them.
  void addFull(uint32_t t,uint32_t tripMeters,uint8_t soc,int16_t motorDeci,int16_t escDeci,int16_t ampsDeci,
               int32_t netDeci,uint32_t regenDeci,int16_t phaseDeci=0){
    size_t n=bytes.size();bytes.resize(n+kRecordBytes);auto*p=bytes.data()+n;
    put32(p,t);put32(p+8,500);put32(p+12,tripMeters);put32(p+16,uint32_t(netDeci));put32(p+20,regenDeci);
    put16(p+24,200);put16(p+26,4000);put16(p+28,ampsDeci);put16(p+30,phaseDeci);
    put16(p+32,motorDeci);put16(p+34,escDeci);p[36]=soc;put32(p+38,0x1001C7F);put16(p+42,crc16(p,42));}
  static bool read(void *arg,uint32_t offset,uint8_t *out,size_t cap,size_t &n,uint32_t &size){auto &f=*static_cast<Fixture*>(arg);f.reads++;if(f.fail)return false;size=uint32_t(f.bytes.size());if(offset>size)return false;n=std::min(cap,size_t(size-offset));memcpy(out,f.bytes.data()+offset,n);return true;}
};
int main(){
  check(crc16(reinterpret_cast<const uint8_t *>("123456789"),9)==0x29b1,"CRC-CCITT reference vector");
  Core c;Sample s;Fixture f;
  for(unsigned i=0;i<10000;i++)f.add(i*200,i==5000?20000:i==5001?-450:210);
  check(c.load(Fixture::read,&f,12),"valid V3 load");
  check(c.overview.duration==1999800 && c.overview.records==10000,"duration/record count");
  auto &peak=c.overview.buckets[uint64_t(1000000)*kBuckets/c.overview.duration];
  check(c.overview.high[kPower]==20000 && c.overview.low[kPower]==-450,"ride extremes preserve spikes and regeneration");
  check(c.overview.fields==7,"only recorded fields are marked present");
  {
    double net=0,regen=0;
    for(unsigned i=0;i+1<10000;i++){double w=i==5000?20000:i==5001?-450:210;net+=w*0.2/3600;if(w<0)regen-=w*0.2/3600;}
    const auto &sum=c.overview.summary;
    check(sum.hasEnergy && std::fabs(sum.netWh-net)<0.05 && std::fabs(sum.regenWh-regen)<0.001,"energy integrated from power");
    check(sum.hasDistance && std::fabs(sum.distanceKm-25*1999.8/3600)<0.01,"distance falls back to integrated speed");
  }
  // The spike and the regen dip share one bucket: its extremes are what the
  // chart draws, so both survive instead of averaging into the steady 210 W.
  check(peak.high[kPower]==20000 && peak.low[kPower]==-450,"bucket keeps its own extremes");
  {
    const auto &steady=c.overview.buckets[0];
    check(steady.high[kPower]==210 && steady.low[kPower]==210,"a steady bucket collapses to one level");
  }
  check(c.sampleAt(1000299,s)&&s.time==1000200&&s.value[1]==-450,"timestamp seek / signed power");
  check(s.mask==7 && s.value[0]==25 && std::fabs(s.value[2]-39.8f)<0.001f,"units and mask");
  check(c.sampleAt(0,s)&&s.time==0,"backwards seek");
  unsigned before=f.reads;
  for(unsigned t=100;t<=10000;t+=100)check(c.sampleAt(t,s)&&s.time<=t,"sequential playback");
  check(f.reads-before<8,"sequential playback must not rescan overview buckets");
  check(c.sampleAt(UINT32_MAX,s)&&s.time==1999800,"clamp seek at end");
  check(!c.load(Fixture::read,&f,13)&&c.error==Error::Format,"wrong ride id");
  Fixture gap;gap.add(0);gap.add(200,210,3);gap.add(400);gap.add(5000);gap.add(5200);
  check(c.load(Fixture::read,&gap,12),"sparse log load");
  check(c.sampleAt(200,s)&&s.mask==3,"missing voltage stays unavailable");
  check(c.sampleAt(3000,s)&&s.mask==0,"timestamp gap stays empty");
  check(c.sampleAt(5000,s)&&s.mask==7,"recover after gap");
  Fixture bad;bad.add(0);bad.add(200);bad.add(400);bad.bytes[kHeaderBytes+kRecordBytes+8]^=1;
  check(c.load(Fixture::read,&bad,12)&&c.overview.badRecords==1,"bad record CRC skipped");
  check(c.sampleAt(200,s)&&s.mask==0,"corruption does not become a false value");
  check(c.sampleAt(400,s)&&s.mask==7,"valid data after corruption");
  // Losing power mid-append leaves a partial record. The complete records
  // before it must still load, because the ride list already counts them.
  Fixture cut;cut.add(0);cut.add(200);cut.add(400);cut.bytes.resize(cut.bytes.size()-10);
  check(c.load(Fixture::read,&cut,12) && c.overview.records==2 && c.overview.duration==200,
        "a partial trailing record is ignored rather than failing the ride");
  check(c.sampleAt(200,s)&&s.mask==7&&s.time==200,"the last complete record before a partial tail still reads");
  cut.bytes.resize(kHeaderBytes+3);
  check(!c.load(Fixture::read,&cut,12)&&c.error==Error::Empty,"a file with no complete record is empty, not readable");
  // Builds before the session clock fix stamped the first record a few ms
  // before the session start, which wrapped to just under 2^32.
  Fixture wrapped;wrapped.add(0xfffffffbU);for(unsigned i=1;i<20;i++)wrapped.add(i*200);
  check(c.load(Fixture::read,&wrapped,12) && c.overview.duration==3800 && c.overview.badRecords==1,
        "a wrapped first timestamp is skipped, not a broken timeline");
  check(c.sampleAt(0,s)&&s.mask==0 && c.sampleAt(400,s)&&s.time==400&&s.mask==7,"a wrapped first record is never shown");
  Fixture ordinaryReversal;ordinaryReversal.add(1000);ordinaryReversal.add(200);
  check(!c.load(Fixture::read,&ordinaryReversal,12)&&c.error==Error::Timeline,
        "an ordinary backwards first timestamp is not mistaken for startup wrap");
  Fixture empty;check(!c.load(Fixture::read,&empty,12)&&c.error==Error::Empty,"empty ride");
  empty.add(0);check(c.load(Fixture::read,&empty,12)&&c.sampleAt(0,s)&&s.mask==7,"single-record ride");
  // Version 2 is the pre-release layout without phase current, and is rejected
  // rather than migrated.
  empty.bytes[4]=2;empty.headerCrc();check(!c.load(Fixture::read,&empty,12),"unsupported version");
  empty.bytes[4]=kLogVersion;check(!c.load(Fixture::read,&empty,12),"header CRC");
  Fixture full;
  // Counters mid-way through a longer trip, regenerating between 200 and 400,
  // then reset (trip and energy together) before 600.
  full.addFull(0,5000,90,452,381,125,1000,200,3100);full.addFull(200,5010,90,460,385,-32,1010,200,-810);
  full.addFull(400,5020,89,471,390,140,1005,210,3550);full.addFull(600,0,89,480,392,150,0,0,3600);
  full.addFull(800,30,88,475,391,110,12,3,2400);
  check(c.load(Fixture::read,&full,12) && c.overview.fields==kAllFields,"all chartable fields load");
  check(c.sampleAt(200,s) && s.mask==kAllFields && std::fabs(s.value[kCurrent]+3.2f)<0.001f &&
        s.value[kBattery]==90 && std::fabs(s.value[kMotorTemp]-46.0f)<0.001f && std::fabs(s.value[kControllerTemp]-38.5f)<0.001f,
        "current, battery and temperatures decode with their scales");
  // Phase current is its own field, several times the pack figure, and signed
  // through regeneration just as pack current is.
  check(std::fabs(s.value[kMotorCurrent]+81.0f)<0.001f,"phase current decodes separately from pack current");
  check(std::fabs(c.overview.high[kMotorCurrent]-360.0f)<0.001f && std::fabs(c.overview.low[kMotorCurrent]+81.0f)<0.001f,
        "phase current extremes");
  check(std::fabs(c.overview.high[kMotorTemp]-48.0f)<0.001f && std::fabs(c.overview.low[kCurrent]+3.2f)<0.001f,"new field extremes");
  check(c.overview.summary.first[kBattery]==90 && c.overview.summary.last[kBattery]==88,"battery start and end");
  check(std::fabs(c.overview.summary.distanceKm-0.05f)<0.0001f,"trip counter growth, ignoring a mid-ride reset");
  // Consumed (net + regen) grows 10 + 5 + 15 = 3.0 Wh, regen 10 + 3 = 1.3 Wh.
  check(std::fabs(c.overview.summary.regenWh-1.3f)<0.001f && std::fabs(c.overview.summary.netWh-1.7f)<0.001f,
        "energy counter growth, ignoring a mid-ride reset");
  check(counterGrowth(5000,5030)==30 && counterGrowth(5000,30)==30,"ride share of a running counter");
  Fixture backwards;backwards.add(0);backwards.add(1000);backwards.add(200);
  check(!c.load(Fixture::read,&backwards,12)&&c.error==Error::Timeline,"nonmonotonic timestamps rejected");
  f.fail=true;check(!c.load(Fixture::read,&f,12)&&c.error==Error::Read,"SD read error");
  std::cout<<"Replay parser, CRC, gaps, peaks, indexed seeking and sequential read budget passed\n";
}
