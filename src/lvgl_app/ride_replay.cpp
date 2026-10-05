#include "ride_replay.h"
#include "app_state.h"
#include "controller_manager.h"
#include <cstring>
#include <new>
static_assert(TELEMETRY_FIELD_SPEED==1 && TELEMETRY_FIELD_POWER==2 && TELEMETRY_FIELD_VOLTAGE==4,
              "Replay masks must match the V2 log format");
namespace {
// Two reads' worth of records between looks at the seek queue.
constexpr unsigned kWindowSliceRecords=2*ride_replay::kReadRecords;
struct Session {
  ride_replay::Core core;
  RideReplayStatus status;
  uint32_t desired=0, request=1;
  bool closing=false;
  // The zoom window the UI wants. windowActive and windowDone belong to whoever
  // builds it: the reader's task, or the caller of rideReplayStatus on the host.
  uint32_t windowRequest=0, windowStart=0, windowEnd=0;
  float windowLow[ride_replay::kFields]={}, windowHigh[ride_replay::kFields]={};
  uint32_t windowActive=0, windowDone=0;
};
Session *session=nullptr;
#ifndef CYD_LVGL_PREVIEW
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
#define LOCK() portENTER_CRITICAL(&mux)
#define UNLOCK() portEXIT_CRITICAL(&mux)
#else
#define LOCK()
#define UNLOCK()
#endif
bool readChunk(void *context,uint32_t offset,uint8_t *buffer,size_t cap,size_t &read,uint32_t &size) {
  auto *s=static_cast<Session *>(context);
  LOCK(); bool closed=s->closing; UNLOCK();
  if (closed) return false;
  bool ok=rideLoggerReadFileChunk(s->status.rideId,offset,buffer,cap,read,size);
  if (ok && size) { LOCK(); if(s->status.state==RideReplayStatus::Loading) s->status.progress=uint64_t(offset)*100/size; UNLOCK(); }
  return ok;
}
void load(Session *s) {
  bool ok=s->core.load(readChunk,s,s->status.rideId);
  ride_replay::Sample sample;
  if(ok) ok=s->core.sampleAt(0,sample);
  LOCK();
  s->status.state=ok?RideReplayStatus::Ready:RideReplayStatus::Failed;
  s->status.duration=s->core.overview.duration;
  s->status.badRecords=s->core.overview.badRecords;
  s->status.sample=sample;
  UNLOCK();
}
// One slice of the newest window request. True while there is more to do.
bool serviceWindow(Session *s,unsigned records) {
  LOCK();
  const uint32_t want=s->windowRequest,start=s->windowStart,end=s->windowEnd;
  float low[ride_replay::kFields],high[ride_replay::kFields];
  memcpy(low,s->windowLow,sizeof(low)); memcpy(high,s->windowHigh,sizeof(high));
  UNLOCK();
  if(want==0 || want==s->windowDone) return false;
  if(want!=s->windowActive) {
    s->windowActive=want;
    // Nothing to build (a cancel, or a ride that cannot be read): the overview stands.
    if(!s->core.beginWindow(start,end,low,high)) { s->windowDone=want; s->windowActive=0; return false; }
  }
  if(!s->core.stepWindow(records)) return true;
  s->windowDone=want; s->windowActive=0;
  if(!s->core.windowFailed()) { LOCK(); if(s->windowRequest==want) s->status.windowId=want; UNLOCK(); }
  return false;
}
#ifdef CYD_LVGL_PREVIEW
// The host has no second task, so a window is built a slice at a time from the
// calls the screen already makes. Tests set the slice to watch a window arrive late.
unsigned previewWindowSlice=~0u;
#endif
#ifndef CYD_LVGL_PREVIEW
void worker(void *arg) {
  auto *s=static_cast<Session *>(arg);
  load(s);
  uint32_t processed=0;
  for (;;) {
    LOCK(); bool closing=s->closing; uint32_t target=s->desired, request=s->request; auto state=s->status.state; UNLOCK();
    if(closing) break;
    if(state==RideReplayStatus::Ready && request!=processed) {
      ride_replay::Sample sample;
      bool ok=s->core.sampleAt(target,sample);
      LOCK();
      if(ok && request==s->request) { s->status.sample=sample; s->status.position=target; }
      if(!ok) s->status.state=RideReplayStatus::Failed;
      UNLOCK();
      processed=request;
    }
    // Seeks come first; a window is built between them, without the idle pause.
    const bool building=state==RideReplayStatus::Ready && serviceWindow(s,kWindowSliceRecords);
    vTaskDelay(building?1:pdMS_TO_TICKS(20));
  }
  delete s;
  vTaskDelete(nullptr);
}
#endif
}
bool rideReplayOpen(uint32_t rideId) {
  rideReplayClose();
  Session *s=new(std::nothrow) Session;
  if(!s) return false;
  s->status.state=RideReplayStatus::Loading; s->status.rideId=rideId;
#ifndef CYD_LVGL_PREVIEW
  if(xTaskCreate(worker,"ride-replay",6144,s,1,nullptr)!=pdPASS) { delete s; return false; }
#else
  load(s);
#endif
  LOCK(); session=s; UNLOCK();
  return true;
}
void rideReplayClose() {
  LOCK(); Session *s=session; session=nullptr; if(s) s->closing=true; UNLOCK();
  if(s) rideLoggerReleaseRead();
#ifdef CYD_LVGL_PREVIEW
  delete s;
#endif
}
void rideReplaySeek(uint32_t position) {
  LOCK(); Session *s=session;
  if(s && s->status.state==RideReplayStatus::Ready) { s->desired=position>s->status.duration?s->status.duration:position; s->request++; }
  UNLOCK();
#ifdef CYD_LVGL_PREVIEW
  if(s && s->status.state==RideReplayStatus::Ready) {
    if(!s->core.sampleAt(s->desired,s->status.sample)) s->status.state=RideReplayStatus::Failed;
    s->status.position=s->desired;
  }
#endif
}
RideReplayStatus rideReplayStatus() {
#ifdef CYD_LVGL_PREVIEW
  if(session && session->status.state==RideReplayStatus::Ready) serviceWindow(session,previewWindowSlice);
#endif
  LOCK(); RideReplayStatus result=session?session->status:RideReplayStatus{}; UNLOCK(); return result;
}
bool rideReplayOverview(ride_replay::Overview &out) {
  LOCK(); bool ready=session && session->status.state==RideReplayStatus::Ready;
  if(ready) out=session->core.overview;
  UNLOCK(); return ready;
}
uint32_t rideReplayRequestWindow(uint32_t start,uint32_t end,const float low[ride_replay::kFields],
                                 const float high[ride_replay::kFields]) {
  LOCK(); Session *s=session; uint32_t id=0;
  if(s && s->status.state==RideReplayStatus::Ready) {
    id=++s->windowRequest; s->windowStart=start; s->windowEnd=end;
    if(low && high) { memcpy(s->windowLow,low,sizeof(s->windowLow)); memcpy(s->windowHigh,high,sizeof(s->windowHigh)); }
  }
  UNLOCK();
#ifdef CYD_LVGL_PREVIEW
  if(id) serviceWindow(s,previewWindowSlice);
#endif
  return id;
}
bool rideReplayWindow(ride_replay::Window &out) {
  LOCK(); Session *s=session;
  // Only the newest request's window is ever copied: an older one may already be
  // overwritten by the build of the one that replaced it.
  const bool ready=s && s->status.state==RideReplayStatus::Ready && s->windowRequest &&
                   s->status.windowId==s->windowRequest && s->core.window();
  if(ready) memcpy(&out,s->core.window(),sizeof(out));
  UNLOCK(); return ready;
}
#ifdef CYD_LVGL_PREVIEW
void previewSetReplayWindowSlice(unsigned records) { previewWindowSlice=records?records:~0u; }
#endif
