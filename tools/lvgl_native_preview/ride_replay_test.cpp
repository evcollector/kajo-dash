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
  void addAt(uint32_t t,int watts,int16_t speedDeci,uint32_t mask=7){size_t n=bytes.size();bytes.resize(n+kRecordBytes);auto*p=bytes.data()+n;put32(p,t);put32(p+8,watts);put16(p+24,speedDeci);put16(p+26,3980);put32(p+38,mask);put16(p+42,crc16(p,42));}
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
  // Every read is a round trip through the SD writer task, so the number of
  // reads is what loading and seeking cost on the device. 27 minutes at 10 Hz.
  {
    Fixture longRide;longRide.bytes[10]=10;longRide.headerCrc();
    const unsigned records=27*60*10;
    for(unsigned i=0;i<records;i++)longRide.add(i*100,200+int(i%97));
    Core r;Sample rs;
    longRide.reads=0;
    check(r.load(Fixture::read,&longRide,12)&&r.overview.records==records,"27 minute ride loads");
    check(longRide.reads<=records/kReadRecords+4,"loading reads whole windows, not a few records at a time");
    // 100x playback: a 100 ms frame moves ten seconds of ride, about a hundred
    // records. Each frame resumes where the last one stopped.
    longRide.reads=0;unsigned frames=0;
    for(uint32_t t=0;t<=r.overview.duration;t+=10000){check(r.sampleAt(t,rs)&&rs.time<=t,"100x playback");frames++;}
    check(longRide.reads<=frames*5/2,"100x playback reads about twice per frame");
    // Dragging forward across the chart is the same walk.
    check(r.sampleAt(0,rs),"rewind");
    longRide.reads=0;
    for(unsigned x=0;x<288;x+=2)check(r.sampleAt(uint64_t(x)*r.overview.duration/287,rs),"drag");
    check(longRide.reads<=144*3,"a forward drag reads a couple of windows per move");
    // A tap anywhere is at most a bucket and a half of records: a few windows.
    longRide.reads=0;unsigned seed=7;
    for(int i=0;i<20;i++){seed=seed*1664525u+1013904223u;check(r.sampleAt(seed%r.overview.duration,rs),"tap");}
    check(longRide.reads<=20*6,"a seek anywhere reads a few windows");
  }
  // Resuming a forward scan must give exactly what a scan from the bucket index
  // gives, across gaps, a corrupt record, missing fields and backward jumps.
  {
    Fixture mixed;
    for(unsigned i=0;i<3000;i++)mixed.add(i*200+(i>1500?20000:0),200+int(i%50),i%7==0?3:7);
    mixed.bytes[kHeaderBytes+2000*kRecordBytes+8]^=1;
    Core resumed,fresh;Sample a,b;
    check(resumed.load(Fixture::read,&mixed,12),"mixed ride loads");
    const uint32_t end=resumed.overview.duration;
    uint32_t t=0;unsigned seed=11;
    for(int step=0;step<400;step++){
      seed=seed*1664525u+1013904223u;
      const unsigned roll=seed>>24;
      if(roll<20) t=t>45000?t-45000:0;                 // back: the index path
      else if(roll<60) t+=200;                          // one record
      else if(roll<200) t+=1000+(seed>>8)%12000;        // playback and skips
      else t+=60000;                                    // far enough to leave the old scan behind
      if(t>end)t=0;
      check(resumed.sampleAt(t,a),"resumed seek");
      check(fresh.load(Fixture::read,&mixed,12)&&fresh.sampleAt(t,b),"fresh seek");
      check(a.time==b.time&&a.mask==b.mask&&std::memcmp(a.value,b.value,sizeof(a.value))==0,
            "a resumed forward scan disagrees with the indexed one");
    }
  }
  // The CRC is table driven now; it must stay the bitwise CCITT it replaced.
  {
    auto bitwise=[](const uint8_t *p,size_t n){uint16_t c=0xffff;while(n--){c^=uint16_t(*p++)<<8;for(int i=0;i<8;i++)c=c&0x8000?(c<<1)^0x1021:c<<1;}return c;};
    uint8_t data[300];unsigned seed=99;
    for(unsigned length=0;length<=300;length+=7){
      for(unsigned i=0;i<length;i++){seed=seed*1664525u+1013904223u;data[i]=uint8_t(seed>>24);}
      check(crc16(data,length)==bitwise(data,length),"table CRC differs from the bitwise one");
    }
  }
  // Zoom windows: columns at the chart's own resolution for a stretch of the ride.
  {
    static const float low[kFields]={0,0,0,0,0,0,0,0},high[kFields]={100,1000,50,100,100,100,100,100};
    auto build=[&](Core &core,uint32_t start,uint32_t end,unsigned slice){
      check(core.beginWindow(start,end,low,high),"window begins");
      unsigned guard=0;while(!core.stepWindow(slice)){check(++guard<2000000,"window never finished");}
    };
    auto quant=[&](unsigned field,float v){float t=(v-low[field])/(high[field]-low[field]);return int(lroundf(254.0f*std::min(1.0f,std::max(0.0f,t))));};
    struct Rec{uint32_t t;int watts;int16_t speed;};
    // 600 s at 5 Hz, every reading different.
    std::vector<Rec> recs;Fixture ride;
    for(unsigned i=0;i<3000;i++){Rec r{i*200,100+int((i*37)%800),int16_t(250+int(i%50)*3)};recs.push_back(r);ride.addAt(r.t,r.watts,r.speed);}
    Core core;check(core.load(Fixture::read,&ride,12),"window ride loads");
    check(core.window()==nullptr,"columns are only allocated for a window");
    // The oracle works from the definition, column by column and not in a stream:
    // a column holds the lowest reading that falls in it, or else the latest
    // reading before it as far as the recording's gap limit reaches.
    auto expected=[&](uint32_t start,uint32_t end,unsigned field,unsigned c){
      const int64_t span=int64_t(end)-start;
      auto columnOf=[&](uint32_t t){int64_t n=(int64_t(t)-start)*(kWindowColumns-1)+span/2;return n>=0?n/span:-((-n+span-1)/span);};
      const int holdColumns=int(uint64_t(1000)*(kWindowColumns-1)/span);
      int own=255;const Rec *latest=nullptr;
      for(const Rec &r:recs){
        const int64_t rc=columnOf(r.t);
        const float v=field==kSpeed?r.speed/10.0f:field==kPower?float(r.watts):39.8f;
        if(rc==int64_t(c)){own=std::min(own,quant(field,v));}
        else if(rc<int64_t(c))latest=&r;
      }
      if(own==255 && latest){
        const int64_t rc=columnOf(latest->t);
        if(int64_t(c)-rc<=holdColumns){const float v=field==kSpeed?latest->speed/10.0f:field==kPower?float(latest->watts):39.8f;own=quant(field,v);}
      }
      return own;
    };
    // A window where every reading spans two columns: each is held across the next.
    build(core,100000,128800,1000);
    check(core.window() && core.window()->start==100000 && core.window()->end==128800,"window bounds");
    unsigned holds=0;
    for(unsigned f:{unsigned(kSpeed),unsigned(kPower),unsigned(kVoltage)})
      for(unsigned c=0;c<kWindowColumns;c++){
        check(core.window()->level[c][f]==expected(100000,128800,f,c),"zoom column disagrees with its definition");
        if(f==kPower && c>0 && core.window()->level[c][f]!=kNoLevel && core.window()->level[c][f]==core.window()->level[c-1][f]) holds++;
      }
    check(holds>20,"sparse readings were not held across their columns");
    for(unsigned c=0;c<kWindowColumns;c++)check(core.window()->level[c][kCurrent]==kNoLevel,"a field the ride never recorded has no levels");
    // A wider window has several readings per column: the lowest one wins.
    build(core,0,600000,1000);
    for(unsigned f:{unsigned(kSpeed),unsigned(kPower)})
      for(unsigned c=0;c<kWindowColumns;c++)check(core.window()->level[c][f]==expected(0,600000,f,c),"wide zoom column disagrees with its definition");
    // Building in slices of any size makes the same window as building in one go.
    {
      static Window oneGo;build(core,250000,330000,1000000);memcpy(&oneGo,core.window(),sizeof(Window));
      for(unsigned slice:{1u,3u,46u,92u,500u}){
        Core other;check(other.load(Fixture::read,&ride,12),"window ride loads again");
        build(other,250000,330000,slice);
        check(memcmp(other.window(),&oneGo,sizeof(Window))==0,"sliced window differs from a one-shot one");
      }
    }
    // Reads: a window costs its records, not the ride. 27 minutes at 10 Hz, an eighth of it.
    {
      Fixture longRide;longRide.bytes[10]=10;longRide.headerCrc();
      for(unsigned i=0;i<16200;i++)longRide.addAt(i*100,200+int(i%97),int16_t(200+int(i%40)));
      Core big;check(big.load(Fixture::read,&longRide,12),"long window ride loads");
      longRide.reads=0;build(big,600000,600000+202500,1000000);
      check(longRide.reads<=2025/kReadRecords+12,"a zoom window reads far more than its own records");
    }
    // A hole in the recording stays a hole once the gap limit has run out.
    {
      Fixture holey;
      for(unsigned i=0;i<3000;i++){if(i>=300 && i<325)continue;holey.addAt(i*200,500,250);}   // 5 s without readings
      Core h;check(h.load(Fixture::read,&holey,12),"holey ride loads");
      build(h,50000,78800,1000);
      const uint32_t last=59800,next=65000;           // the readings either side of the hole
      auto columnOf=[&](uint32_t t){return int((int64_t(t)-50000)*(kWindowColumns-1)+14400)/28800;};
      const int a=columnOf(last),b=columnOf(next);
      check(h.window()->level[a][kPower]!=kNoLevel && h.window()->level[a+5][kPower]!=kNoLevel,"readings before the hole are drawn and held");
      check(h.window()->level[(a+b)/2][kPower]==kNoLevel,"the middle of a five second hole was filled");
      check(h.window()->level[b][kPower]!=kNoLevel,"the reading after the hole is drawn");
    }
    // A reading that fails its CRC, or lacks the field, ends the held run.
    {
      Fixture damaged;
      for(unsigned i=0;i<3000;i++)damaged.addAt(i*200,500+int(i%9)*10,250,i>=400&&i<403?3:7);  // three without voltage
      damaged.bytes[kHeaderBytes+350*kRecordBytes+8]^=1;                                        // one corrupt
      Core d;check(d.load(Fixture::read,&damaged,12)&&d.overview.badRecords==1,"damaged ride loads");
      build(d,60000,88800,1000);
      auto columnOf=[&](uint32_t t){return int((int64_t(t)-60000)*(kWindowColumns-1)+14400)/28800;};
      const int before=columnOf(349*200),after=columnOf(351*200);
      check(d.window()->level[before][kPower]!=kNoLevel && d.window()->level[after][kPower]!=kNoLevel,"readings around the corrupt one are drawn");
      check(d.window()->level[before+1][kPower]==kNoLevel,"a held value crossed a corrupt reading");
      const int volt=columnOf(400*200);   // three readings without voltage start here, two columns apart
      check(d.window()->level[volt+1][kPower]!=kNoLevel && d.window()->level[volt+5][kPower]!=kNoLevel,"a field that is there stopped being drawn");
      check(d.window()->level[volt+1][kVoltage]==kNoLevel && d.window()->level[volt+5][kVoltage]==kNoLevel,"a missing field was filled in");
      check(d.window()->level[volt+6][kVoltage]!=kNoLevel,"the field is drawn again once it returns");
    }
    // Windows that overhang the ride, and ones that make no sense.
    {
      Core edge;check(edge.load(Fixture::read,&ride,12),"edge ride loads");
      build(edge,0,28800,1000);
      check(edge.window()->level[0][kPower]!=kNoLevel && edge.window()->level[kWindowColumns-1][kPower]!=kNoLevel,"a window at the start is full");
      build(edge,590000,619000,1000);   // the ride ends at 599.8 s, a third of the way along
      check(edge.window()->level[10][kPower]!=kNoLevel && edge.window()->level[kWindowColumns-1][kPower]==kNoLevel,"a window past the end is empty beyond it");
      check(!edge.beginWindow(5000,5000,low,high) && !edge.beginWindow(9000,5000,low,high),"an empty window is refused");
      ride.fail=true;
      check(edge.beginWindow(100000,128800,low,high),"a window begins before the card fails");
      unsigned guard=0;while(!edge.stepWindow(100)&&++guard<1000){}
      check(edge.windowFailed(),"a read error did not fail the window");
      ride.fail=false;
    }
  }
  f.fail=true;check(!c.load(Fixture::read,&f,12)&&c.error==Error::Read,"SD read error");
  std::cout<<"Replay parser, CRC, gaps, peaks, indexed and resumed seeking, zoom windows and the read budget passed\n";
}
