#include "ride_replay.h"
#include "app_state.h"
#include "controller_manager.h"
#include <new>
static_assert(TELEMETRY_FIELD_SPEED==1 && TELEMETRY_FIELD_POWER==2 && TELEMETRY_FIELD_VOLTAGE==4,
              "Replay masks must match the V2 log format");
namespace {
struct Session {
  ride_replay::Core core;
  RideReplayStatus status;
  uint32_t desired=0, request=1;
  bool closing=false;
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
    vTaskDelay(pdMS_TO_TICKS(20));
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
  LOCK(); RideReplayStatus result=session?session->status:RideReplayStatus{}; UNLOCK(); return result;
}
bool rideReplayOverview(ride_replay::Overview &out) {
  LOCK(); bool ready=session && session->status.state==RideReplayStatus::Ready;
  if(ready) out=session->core.overview;
  UNLOCK(); return ready;
}
