// Included by screens.cpp after the canonical menu constructors. This keeps
// replay-specific state together while reusing the shared navigation buttons.
using ride_replay::kFields;
using ride_replay::kMaxCharts;
static constexpr int kReplayRowBase=12000;
static constexpr int kReplayPlay=12010, kReplayBack=12011, kReplayForward=12012, kReplayRate=12013;
static constexpr int kReplayCharts=12014, kReplaySummary=12015, kReplayPopupDone=12016, kReplayChartsReset=12017;
static constexpr int kReplayDeleteRide=12018, kReplayDeleteConfirm=12019;
static constexpr int kReplayCountBase=12020, kReplayZoomOut=12024, kReplayZoomIn=12025;
static constexpr int kReplaySlotBase=12030, kReplayFieldBase=12040;
static uint32_t replayRideId=0, replayPosition=0, replayClock=0;
static bool replayPlaying=false, replayHasGraph=false, replayOpenFailed=false;
static uint8_t replayRate=1;
static lv_obj_t *replayChart=nullptr, *replayTime=nullptr, *replayPlayButton=nullptr, *replayRateButton=nullptr;
static lv_obj_t *replayBackButton=nullptr, *replayForwardButton=nullptr, *replayChartsButton=nullptr;
static lv_timer_t *replayTimer=nullptr;
static ride_replay::Overview *replayGraph=nullptr;
static RideReplayStatus replayStatus;
static float replayLow[kFields]={},replayHigh[kFields]={};
static constexpr int kReplayPlotX=28, kReplayPlotWidth=288, kReplayBubbleGap=18;
// The charts share a fixed area between the header and the transport buttons,
// split evenly between one to four bands. The trace uses the full band above
// its black separator row; the field name overlays its bottom-left corner.
static constexpr int kReplayChartY=42, kReplayChartArea=159;
static constexpr uint8_t kReplayRates[]={1,2,4,10,50,100};
// Three quick taps on the same skip button widen the step to 30 s; it drops
// back to 10 s once the skip buttons have been left alone for two seconds.
static constexpr uint32_t kReplaySkipShort=10000, kReplaySkipLong=30000;
static constexpr uint32_t kReplayQuickTapMs=700, kReplayLongSkipHoldMs=2000;
static uint32_t replaySkipStep=kReplaySkipShort, replayLastSkipTap=0;
static uint8_t replaySkipTaps=0;
static int replayLastSkipId=0;
// Which field each chart shows. All four slots stay distinct, including the
// hidden ones, so showing another chart never duplicates a visible one.
static constexpr uint8_t kReplayDefaultCount=3;
static constexpr uint8_t kReplayDefaultFields[kMaxCharts]={ride_replay::kSpeed,ride_replay::kPower,ride_replay::kVoltage,ride_replay::kCurrent};
static uint8_t replayChartCount=kReplayDefaultCount;
static uint8_t replayChartField[kMaxCharts]={ride_replay::kSpeed,ride_replay::kPower,ride_replay::kVoltage,ride_replay::kCurrent};
// One popup at a time: the summary (whose delete confirmation returns to it
// on Cancel), the chart layout, or the field list for one chart (which
// returns to the layout).
enum class ReplayPopup : uint8_t { None, Summary, DeleteConfirm, Charts, Fields };
static ReplayPopup replayPopupKind=ReplayPopup::None;
static lv_obj_t *replayPopup=nullptr, *replaySlotStack=nullptr, *replayCountButtons[kMaxCharts]={};
static uint8_t replayEditSlot=0;

// ── Zoom: state and the small questions asked of it ──────────────────────────
// Each step halves the stretch of ride the chart shows, around the cursor. The
// overview is only 128 buckets, so a zoomed view asks the reader for a window:
// the chart's own columns for that stretch, built a few reads at a time. Until
// they arrive the overview is stretched over the view.
static_assert(kReplayPlotWidth==int(ride_replay::kWindowColumns),"a window has one column per plot pixel");
static constexpr int kReplayStripHeight=2, kReplayStripRows=3;  // under the charts: the view's place in the ride
static constexpr uint32_t kReplayZoomMs=2*cyd_ui::kMotionMs;
static uint8_t replayZoom=0;                         // steps in; 0 is the whole ride
static uint32_t replayViewStart=0, replayViewEnd=0;  // the stretch on screen (mid-zoom, the frame in flight), ms
static ride_replay::Window *replayDetail=nullptr;    // the reader's columns for the window below, once copied
static bool replayDetailValid=false;
static uint32_t replayViewRequest=0;                 // a window asked for the view and not yet taken
static uint32_t replayNextRequest=0, replayNextStart=0, replayNextEnd=0;  // the next page, built ahead of the cursor
static bool replayZooming=false;
static int32_t replayZoomPhase=0;                    // 0..1000 through the animation
static uint32_t replayZoomFrom[2]={}, replayZoomTo[2]={}, replayZoomAnchor=0;
static float replayZoomFraction[2]={};               // where the anchor sits in the first and last view, 0..1
static lv_obj_t *replayZoomOutButton=nullptr, *replayZoomInButton=nullptr;
static int8_t replayZoomInShown=-1, replayZoomOutShown=-1;

static uint32_t replayDuration() { return replayGraph?replayGraph->duration:0; }
static uint32_t replayViewSpan() { return LV_MAX(1U,replayViewEnd-replayViewStart); }
static bool replayViewWhole() { return replayViewStart==0 && replayViewEnd==replayDuration(); }
// The narrowest view is where a recorded reading is two columns wide, and a ride
// too short for that does not zoom at all.
static uint8_t replayMaxZoom() {
  if(!replayGraph) return 0;
  const uint32_t narrowest=LV_MAX(10000U,uint32_t(replayGraph->periodMs)*(kReplayPlotWidth/2));
  uint8_t level=0;
  while(level<8 && (replayGraph->duration>>(level+1))>=narrowest) level++;
  return level;
}
static bool replayCanZoomIn() { return replayHasGraph && replayStatus.state==RideReplayStatus::Ready && replayZoom<replayMaxZoom(); }
static bool replayCanZoomOut() { return replayHasGraph && replayStatus.state==RideReplayStatus::Ready && replayZoom>0; }
// The screen column of a moment in the ride, as seen through the current view.
static int replayXFor(uint32_t position,int ox) {
  const int64_t into=LV_MAX(int64_t(0),LV_MIN(int64_t(replayViewSpan()),int64_t(position)-int64_t(replayViewStart)));
  return int(ox+kReplayPlotX+uint64_t(into)*(kReplayPlotWidth-1)/replayViewSpan());
}

static int replayBandHeight() { return kReplayChartArea/replayChartCount; }
static int replayBase() { return replayBandHeight()-2; }
static int replayMaxLevel() { return replayBase()-3; }
static bool replayFieldRecorded(uint8_t f) { return replayGraph && (replayGraph->fields&(1U<<f)); }
// These fields include zero in their scale; signed power and currents may
// extend below it when the ride contains regenerative samples.
static bool replayIncludesZero(uint8_t f) {
  return f==ride_replay::kSpeed || f==ride_replay::kPower || f==ride_replay::kCurrent ||
         f==ride_replay::kMotorCurrent;
}

static const char *replayFieldName(uint8_t f) {
  switch(f) {
    case ride_replay::kSpeed: return txt("Speed","Nopeus","Tempo","Vitesse","Veloc.","Veloc.");
    case ride_replay::kPower: return txt("Power","Teho","Leistung","Puiss.","Potencia","Potenza");
    case ride_replay::kVoltage: return txt("Voltage","Jännite","Spannung","Tension","Voltaje","Tensione");
    case ride_replay::kCurrent: return txt("Current","Virta","Strom","Courant","Corriente","Corrente");
    case ride_replay::kMotorCurrent: return txt("Phase amps","Vaihevirta","Phasenstrom","Courant phase","Amp. fase","Amp. fase");
    case ride_replay::kBattery: return txt("Battery","Akku","Akku","Batterie","Batería","Batteria");
    case ride_replay::kMotorTemp: return txt("Motor temp","Moottorin lämpö","Motortemp.","Temp. moteur","Temp. motor","Temp. motore");
    default: return txt("Controller temp","Ohjaimen lämpö","Reglertemp.","Temp. contrôleur","Temp. control.","Temp. centralina");
  }
}
static float replayDisplayValue(uint8_t f,float v) { return f==ride_replay::kSpeed?displaySpeed(v):v; }
static void formatReplayValue(char *out,size_t size,uint8_t f,float v) {
  char number[16];
  switch(f) {
    case ride_replay::kSpeed: formatSpeedValue(number,sizeof(number),v); snprintf(out,size,"%s %s",number,speedUnitLabel()); break;
    case ride_replay::kPower: snprintf(out,size,"%.0f W%s",v,v<0?" (regen)":""); break;
    case ride_replay::kVoltage: snprintf(out,size,"%.1f V",v); break;
    case ride_replay::kCurrent:
    case ride_replay::kMotorCurrent: snprintf(out,size,"%.1f A",v); break;
    case ride_replay::kBattery: snprintf(out,size,"%.0f %%",v); break;
    default: snprintf(out,size,"%.0f °C",v); break;
  }
}
static int replayY(int slot,float value) {
  const uint8_t f=replayChartField[slot];
  float t=(value-replayLow[f])/(replayHigh[f]-replayLow[f]);
  t=LV_MAX(0.0f,LV_MIN(1.0f,t));
  return slot*replayBandHeight()+replayBase()-lroundf(t*replayMaxLevel());
}
// wrap=false keeps a string that overruns its area on one line. The scale
// labels live in a 25 px gutter: wrapped there, one number becomes two stacked
// fragments that read as extra scale values.
static void replayText(lv_draw_ctx_t *ctx,int x,int y,int w,const char *text,lv_color_t color,
                       lv_text_align_t align=LV_TEXT_ALIGN_LEFT,bool wrap=true) {
  lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
  d.font=&lv_font_rajdhani_12; d.color=color; d.align=align;
  if(!wrap) d.flag=lv_text_flag_t(d.flag|LV_TEXT_FLAG_EXPAND);
  lv_area_t a={lv_coord_t(x),lv_coord_t(y),lv_coord_t(x+w-1),lv_coord_t(y+15)};
  lv_draw_label(ctx,&d,&a,text,nullptr);
}
static void replayLine(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,lv_color_t color,int width=1) {
  lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=color; d.width=width;
  lv_point_t a={lv_coord_t(x1),lv_coord_t(y1)},b={lv_coord_t(x2),lv_coord_t(y2)};
  lv_draw_line(ctx,&d,&a,&b);
}
// Like Efficiency, prepare a height per physical column once, then draw
// clipped horizontal runs. 255 marks a gap.
//
// One height per column: the interval's lowest reading defines both the trace
// and its under-curve fill. This keeps the fill edge exactly on the trace.
//
// Neither interpolating between bucket centres nor averaging a bucket's
// readings can be used here. Both put the trace somewhere the ride never was:
// a mean pulls every column towards the middle of the band, and interpolation
// narrows a bucket that reached the scale's end to the single column at its
// centre -- a one-pixel spike, which is what a rider reads as "the trace never
// reaches zero" while coasting at 0 W for half a minute. The same mapping the
// cursor uses picks the bucket, so the line under the cursor covers the
// reading its bubble shows.
// Recomputed whenever the chart layout, a chart's field or the view changes.
// Zoomed in, a column takes the reader's window where it has one for that moment
// and the overview, stretched, where it has not.
static uint8_t replayHeights[kMaxCharts][kReplayPlotWidth];
static void prepareReplayGraph() {
  if(!replayGraph) return;
  const int maxLevel=replayMaxLevel();
  const bool whole=replayViewWhole();
  const uint64_t span=replayViewSpan();
  const uint32_t duration=LV_MAX(1U,replayGraph->duration);
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    memset(replayHeights[slot],255,kReplayPlotWidth);
    auto level=[&](float v) {
      return uint8_t(lroundf(float(maxLevel)*LV_MAX(0.0f,LV_MIN(1.0f,(v-replayLow[f])/(replayHigh[f]-replayLow[f])))));
    };
    for(int x=0;x<kReplayPlotWidth;x++) {
      // The moment this column stands for. The whole ride is the overview's own
      // columns, mapped exactly as they always were.
      const uint32_t t=whole?0:replayViewStart+uint32_t(uint64_t(x)*span/(kReplayPlotWidth-1));
      if(!whole && replayDetailValid && t>=replayDetail->start && t<=replayDetail->end) {
        // The reader's column for that moment. Heights come from the same
        // scale the overview uses, so a zoomed trace matches the full one.
        const uint64_t detailSpan=LV_MAX(1U,replayDetail->end-replayDetail->start);
        const unsigned column=LV_MIN(unsigned(kReplayPlotWidth-1),
                                     unsigned((uint64_t(t-replayDetail->start)*(kReplayPlotWidth-1)+detailSpan/2)/detailSpan));
        const uint8_t q=replayDetail->level[column][f];
        if(q!=ride_replay::kNoLevel) replayHeights[slot][x]=uint8_t(lroundf(float(maxLevel)*float(q)/254.0f));
        continue;
      }
      const unsigned b=whole?LV_MIN(ride_replay::kBuckets-1,unsigned(x)*ride_replay::kBuckets/(kReplayPlotWidth-1))
                            :LV_MIN(ride_replay::kBuckets-1,unsigned(uint64_t(t)*ride_replay::kBuckets/duration));
      const auto &bucket=replayGraph->buckets[b];
      if(!(bucket.mask&(1<<f)) || (bucket.gaps&(1<<f))) continue;
      replayHeights[slot][x]=level(bucket.low[f]);
    }
  }
}
// ── What is drawn over the traces ─────────────────────────────────────────────
// The cursor, and for every chart one dot and one bubble. They are kept as data
// so a tick can compare the overlay it last invalidated with the one it would
// draw now, and repaint only the pixels that differ. The whole chart is about
// 55,000 px; a cursor that moved or a value that changed is a few thousand.
struct ReplayBubble {
  bool shown=false;  // the field was recorded, so it has a bubble
  bool dot=false;    // and the sample holds it, so it has a dot as well
  lv_area_t dotArea={},bubble={},text={};
  char caption[32]="";
};
struct ReplayOverlay {
  bool valid=false;  // the graph is ready; until then only a message is drawn
  int cursor=0,sampleCursor=0,top=0,bottom=0;
  ReplayBubble slot[kMaxCharts];
};
static ReplayOverlay replayShown;  // as of the last invalidation: what the next repaint will show

static void replayComputeOverlay(ReplayOverlay &o,const lv_area_t &bounds) {
  o=ReplayOverlay();
  if(!replayHasGraph || replayStatus.state!=RideReplayStatus::Ready) return;
  const int ox=bounds.x1,oy=bounds.y1,bandHeight=replayBandHeight();
  const uint32_t shownPosition=replayPlaying?replayStatus.position:replayPosition;
  o.valid=true;
  o.cursor=replayXFor(shownPosition,ox);
  // The reader decodes seeks on its own task, so while a finger drags the
  // cursor the newest sample is still a few milliseconds behind it. Draw that
  // sample rather than blanking every bubble to "--" until the drag stops: the
  // dot sits at the moment it belongs to, so the lag is visible but honest.
  o.sampleCursor=replayXFor(replayStatus.position,ox);
  o.top=oy;
  o.bottom=oy+replayChartCount*bandHeight-2;
  // Bubbles sit left of the cursor from a quarter of the way across the plot
  // onward and right of it before, all together. The point is fixed, never
  // derived from the current values, so a bubble that widens as its value
  // changes (power entering regen) cannot make them hop back and forth. A
  // wide bubble just left of it may run over the scale labels.
  const bool bubblesLeft=o.cursor>=ox+kReplayPlotX+kReplayPlotWidth/4;
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    ReplayBubble &b=o.slot[slot];
    if(!replayFieldRecorded(f)) continue;
    b.shown=true;
    const bool valid=replayStatus.sample.mask&(1<<f);
    if(valid) formatReplayValue(b.caption,sizeof(b.caption),f,replayStatus.sample.value[f]);
    else snprintf(b.caption,sizeof(b.caption),"--");
    if(valid) {
      const int pointY=oy+replayY(slot,replayStatus.sample.value[f]);
      b.dot=true;
      b.dotArea={lv_coord_t(o.sampleCursor-3),lv_coord_t(pointY-3),lv_coord_t(o.sampleCursor+3),lv_coord_t(pointY+3)};
    }
    lv_point_t textSize; lv_txt_get_size(&textSize,b.caption,&lv_font_rajdhani_12,0,0,250,LV_TEXT_FLAG_NONE);
    // Fixed height, centred in the band, so bubbles never jump with the data.
    const int w=LV_MIN(250,textSize.x+10);
    int x=bubblesLeft?o.cursor-kReplayBubbleGap-w:o.cursor+kReplayBubbleGap;
    // Kept on screen, but free to cover the scale labels on the left.
    x=LV_MAX(ox+1,LV_MIN(ox+316-w,x));
    const int y=oy+slot*bandHeight+(bandHeight-1-20)/2;
    b.bubble={lv_coord_t(x),lv_coord_t(y),lv_coord_t(x+w),lv_coord_t(y+19)};
    b.text={lv_coord_t(x+5),lv_coord_t(y+2),lv_coord_t(x+5+(w-8)-1),lv_coord_t(y+2+15)};
  }
}
static bool replayReaches(const lv_area_t &clip,const lv_area_t &area) {
  return area.x1<=clip.x2 && area.x2>=clip.x1 && area.y1<=clip.y2 && area.y2>=clip.y1;
}
static bool replaySameArea(const lv_area_t &a,const lv_area_t &b) {
  return a.x1==b.x1 && a.y1==b.y1 && a.x2==b.x2 && a.y2==b.y2;
}
// Something that moved: one dirty area when its old and new places touch, since
// LVGL keeps 32 dirty areas per frame and repaints the whole screen beyond that.
static void replayInvalidateMoved(bool hadOld,const lv_area_t &was,bool hasNew,const lv_area_t &now) {
  if(hadOld && hasNew && was.x1<=now.x2+1 && now.x1<=was.x2+1 && was.y1<=now.y2+1 && now.y1<=was.y2+1) {
    const lv_area_t both={LV_MIN(was.x1,now.x1),LV_MIN(was.y1,now.y1),LV_MAX(was.x2,now.x2),LV_MAX(was.y2,now.y2)};
    lv_obj_invalidate_area(replayChart,&both);
    return;
  }
  if(hadOld) lv_obj_invalidate_area(replayChart,&was);
  if(hasNew) lv_obj_invalidate_area(replayChart,&now);
}
// Repaints what the overlay changed since the last call and nothing else. The
// traces underneath are redrawn only where the cursor, a dot or a bubble was or
// is; an overlay that did not change costs no repaint at all.
static void replayInvalidateOverlay() {
  if(!replayChart) return;
  lv_area_t bounds; lv_obj_get_coords(replayChart,&bounds);
  ReplayOverlay now; replayComputeOverlay(now,bounds);
  const ReplayOverlay &was=replayShown;
  if(was.valid && now.valid) {
    if(was.cursor!=now.cursor) {
      const lv_area_t a={lv_coord_t(was.cursor-1),lv_coord_t(was.top),lv_coord_t(was.cursor+1),lv_coord_t(was.bottom)};
      const lv_area_t b={lv_coord_t(now.cursor-1),lv_coord_t(now.top),lv_coord_t(now.cursor+1),lv_coord_t(now.bottom)};
      replayInvalidateMoved(true,a,true,b);
    }
    for(unsigned slot=0;slot<kMaxCharts;slot++) {
      const ReplayBubble &a=was.slot[slot],&b=now.slot[slot];
      if(a.dot!=b.dot || (a.dot && !replaySameArea(a.dotArea,b.dotArea)))
        replayInvalidateMoved(a.dot,a.dotArea,b.dot,b.dotArea);
      if(a.shown!=b.shown || (b.shown && !replaySameArea(a.bubble,b.bubble)))
        replayInvalidateMoved(a.shown,a.bubble,b.shown,b.bubble);
      else if(b.shown && strcmp(a.caption,b.caption)!=0) lv_obj_invalidate_area(replayChart,&b.text);
    }
  } else if(was.valid!=now.valid) {
    lv_obj_invalidate(replayChart);
  }
  replayShown=now;
}
// The whole chart: a new layout, the ride finishing loading, an error. The
// overlay is recorded as it will be drawn, so the next tick compares with that.
static void replayInvalidateChart() {
  if(!replayChart) return;
  lv_obj_invalidate(replayChart);
  lv_area_t bounds; lv_obj_get_coords(replayChart,&bounds);
  replayComputeOverlay(replayShown,bounds);
}

static void replayDraw(lv_event_t *event) {
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(event);
  lv_area_t bounds; lv_obj_get_coords(replayChart,&bounds);
  int ox=bounds.x1,oy=bounds.y1;
  if(!replayHasGraph || replayStatus.state!=RideReplayStatus::Ready) {
    const char *message=replayStatus.state==RideReplayStatus::Loading && !replayOpenFailed
      ?txt("Loading ride...","Ladataan ajoa...","Fahrt wird geladen...","Chargement...","Cargando viaje...","Caricamento...")
      :txt("Ride unavailable","Ajo ei saatavilla","Fahrt nicht verfügbar","Trajet indisponible","Viaje no disponible","Viaggio non disponibile");
    replayText(ctx,ox+38,oy+54,270,message,lv_color_white()); return;
  }
  // The repaint may be a strip around the cursor or one bubble rather than the
  // chart: whatever the clip cannot reach is skipped, which is what keeps a
  // moving cursor cheap. The pixels inside the clip are the same either way.
  const lv_area_t &clip=*ctx->clip_area;
  const int firstColumn=clip.x1-ox-kReplayPlotX, lastColumn=clip.x2-ox-kReplayPlotX;
  const int bandHeight=replayBandHeight(), baseRow=replayBase(), maxLevel=replayMaxLevel();
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    auto color=c565(cyd_ui::kReplayTrace565[f]);
    int top=oy+slot*bandHeight;
    const int base=top+baseRow;
    // A band's own pixels are rows top..base; its scale labels reach four rows
    // above and a few below.
    if(clip.y2<top-4 || clip.y1>base+4) continue;
    lv_draw_rect_dsc_t bg; lv_draw_rect_dsc_init(&bg);
    bg.bg_color=c565(cyd_ui::kReplayBackground565[f]); bg.bg_opa=LV_OPA_COVER;
    lv_area_t area={lv_coord_t(ox+kReplayPlotX),lv_coord_t(top),lv_coord_t(ox+315),lv_coord_t(base)};
    lv_draw_rect(ctx,&bg,&area);
    lv_draw_rect_dsc_t fill;lv_draw_rect_dsc_init(&fill);
    fill.bg_color=c565(cyd_ui::kReplayFill565[f]);
    if(!replayFieldRecorded(f)) {
      replayText(ctx,ox+kReplayPlotX,top+(baseRow-14)/2,kReplayPlotWidth,
                 txt("Not recorded","Ei tallennettu","Nicht aufgezeichnet","Non enregistré","No registrado","Non registrato"),
                 cyd_ui::secondaryText(),LV_TEXT_ALIGN_CENTER);
      replayText(ctx,ox+kReplayPlotX+4,base-int(lv_font_rajdhani_12.line_height)+2,
                 200,replayFieldName(f),color);
      continue;
    }
    // Efficiency's clipped horizontal-span renderer, with a flat color.
    const int first=LV_MAX(0,firstColumn);
    const int last=LV_MIN(kReplayPlotWidth-1,lastColumn);
    for(int level=LV_MAX(0,base-clip.y2);level<=LV_MIN(maxLevel,base-clip.y1);level++) {
      int start=-1;
      for(int x=first;x<=last+1;x++) {
        bool covered=x<=last && replayHeights[slot][x]!=255 && replayHeights[slot][x]>=level;
        if(covered && start<0) start=x;
        if(covered || start<0) continue;
        lv_area_t span={lv_coord_t(ox+kReplayPlotX+start),lv_coord_t(base-level),
                        lv_coord_t(ox+kReplayPlotX+x-1),lv_coord_t(base-level)};
        lv_draw_rect(ctx,&fill,&span);start=-1;
      }
    }
    // The line and fill share each interval's minimum height. Only the columns
    // the repaint reaches are drawn, and two more either side: a segment joins
    // the column before it to its own.
    int previousX=-1,previousY=0;
    for(int x=LV_MAX(0,firstColumn-2);x<=LV_MIN(kReplayPlotWidth-1,lastColumn+2);x++) {
      if(replayHeights[slot][x]==255) { previousX=-1;continue; }
      int y=base-replayHeights[slot][x];
      if(previousX>=0) replayLine(ctx,previousX,previousY,ox+kReplayPlotX+x,y,color);
      previousX=ox+kReplayPlotX+x;previousY=y;
    }
    // Anchor the field name's visible glyphs against the bottom-left corner.
    replayText(ctx,ox+kReplayPlotX+4,base-int(lv_font_rajdhani_12.line_height)+2,
               200,replayFieldName(f),color);
    // Keep each axis label's visible glyphs inside its own band.
    char axis[16];
    const int axisY[2]={top-4,base-int(lv_font_rajdhani_12.line_height)+2};
    for(int row=0;row<2;row++) {
      float value=replayDisplayValue(f,row==0?replayHigh[f]:replayLow[f]);
      // Four characters is what the gutter holds, so thousands lose their
      // decimal once they reach five digits: 12345 W reads 12k, not 12.3k.
      if(fabsf(value)>=9950) snprintf(axis,sizeof(axis),"%.0fk",value/1000);
      else if(fabsf(value)>=1000) snprintf(axis,sizeof(axis),"%.1fk",value/1000);
      else if(fabsf(value-roundf(value))>0.05f) snprintf(axis,sizeof(axis),"%.1f",value);
      else snprintf(axis,sizeof(axis),"%.0f",value);
      replayText(ctx,ox,axisY[row],kReplayPlotX-3,axis,color,LV_TEXT_ALIGN_RIGHT,false);
    }
  }
  // Where a zoomed view sits in the ride: a thin track under the charts with
  // the stretch on screen lit. The whole ride has nothing to say, so no track.
  if(!replayViewWhole()) {
    const lv_area_t track={lv_coord_t(ox+kReplayPlotX),lv_coord_t(oy+kReplayChartArea),
                           lv_coord_t(ox+kReplayPlotX+kReplayPlotWidth-1),lv_coord_t(oy+kReplayChartArea+kReplayStripHeight-1)};
    if(replayReaches(clip,track)) {
      const uint64_t duration=LV_MAX(1U,replayGraph->duration);
      const int from=int(uint64_t(replayViewStart)*(kReplayPlotWidth-1)/duration);
      const int to=LV_MAX(from+2,int(uint64_t(replayViewEnd)*(kReplayPlotWidth-1)/duration));
      lv_draw_rect_dsc_t bar; lv_draw_rect_dsc_init(&bar);
      bar.bg_color=cyd_ui::mutedBorder();
      lv_draw_rect(ctx,&bar,&track);
      bar.bg_color=cyd_ui::chromeAccent();
      const lv_area_t lit={lv_coord_t(track.x1+from),track.y1,lv_coord_t(LV_MIN(int(track.x2),track.x1+to)),track.y2};
      lv_draw_rect(ctx,&bar,&lit);
    }
  }
  ReplayOverlay overlay; replayComputeOverlay(overlay,bounds);
  // A grey core with dimmer shoulders reads as a cursor without glaring.
  replayLine(ctx,overlay.cursor-1,overlay.top,overlay.cursor-1,overlay.bottom,lv_color_hex(0x505050));
  replayLine(ctx,overlay.cursor+1,overlay.top,overlay.cursor+1,overlay.bottom,lv_color_hex(0x505050));
  replayLine(ctx,overlay.cursor,overlay.top,overlay.cursor,overlay.bottom,lv_color_hex(0xB4B4B4));
  for(int slot=0;slot<replayChartCount;slot++) {
    const ReplayBubble &b=overlay.slot[slot];
    if(!b.shown) continue;
    auto color=c565(cyd_ui::kReplayTrace565[replayChartField[slot]]);
    if(b.dot && replayReaches(clip,b.dotArea)) {
      lv_draw_rect_dsc_t dot; lv_draw_rect_dsc_init(&dot); dot.radius=LV_RADIUS_CIRCLE;
      dot.bg_color=color; dot.border_color=lv_color_white(); dot.border_width=1;
      lv_draw_rect(ctx,&dot,&b.dotArea);
    }
    if(!replayReaches(clip,b.bubble)) continue;
    lv_draw_rect_dsc_t bubble; lv_draw_rect_dsc_init(&bubble); bubble.radius=4;
    bubble.bg_color=lv_color_black(); bubble.border_color=color; bubble.border_width=1;
    lv_draw_rect(ctx,&bubble,&b.bubble);
    replayText(ctx,b.bubble.x1+5,b.bubble.y1+2,(b.bubble.x2-b.bubble.x1)-8,b.caption,lv_color_white());
  }
}
static void replayTransportDraw(lv_event_t *event) {
  lv_area_t a; lv_obj_get_coords(lv_event_get_target(event),&a);
  int cx=(a.x1+a.x2)/2, cy=(a.y1+a.y2)/2;
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(event);
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color=lv_color_white();
  if(replayPlaying) {
    lv_area_t left={lv_coord_t(cx-6),lv_coord_t(cy-7),lv_coord_t(cx-3),lv_coord_t(cy+7)};
    lv_area_t right={lv_coord_t(cx+3),lv_coord_t(cy-7),lv_coord_t(cx+6),lv_coord_t(cy+7)};
    lv_draw_rect(ctx,&d,&left);lv_draw_rect(ctx,&d,&right);
  } else {
    lv_point_t points[3]={{lv_coord_t(cx-5),lv_coord_t(cy-8)},
                         {lv_coord_t(cx+8),lv_coord_t(cy)},
                         {lv_coord_t(cx-5),lv_coord_t(cy+8)}};
    lv_draw_triangle(ctx,&d,points);
  }
}
// The chart button's glyph is a miniature of the current layout: one bar per
// chart, in that chart's field colour.
static void replayChartsGlyphDraw(lv_event_t *event) {
  lv_area_t a; lv_obj_get_coords(lv_event_get_target(event),&a);
  const int cx=(a.x1+a.x2)/2, gap=replayChartCount>3?1:2, total=18;
  const int bar=(total-(replayChartCount-1)*gap)/replayChartCount;
  const int stackHeight=replayChartCount*bar+(replayChartCount-1)*gap;
  const int firstTop=a.y1+(a.y2-a.y1+1-stackHeight)/2;
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(event);
  for(int slot=0;slot<replayChartCount;slot++) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.radius=1;
    d.bg_color=c565(cyd_ui::kReplayTrace565[replayChartField[slot]]);
    const int top=firstTop+slot*(bar+gap);
    lv_area_t r={lv_coord_t(cx-11),lv_coord_t(top),lv_coord_t(cx+11),lv_coord_t(top+bar-1)};
    lv_draw_rect(ctx,&d,&r);
  }
}
// A magnifying glass with a plus (zoom in) or a minus (zoom out), dimmed when
// there is nowhere further to zoom.
static void replayZoomGlyphDraw(lv_event_t *event) {
  lv_obj_t *button=lv_event_get_target(event);
  const bool zoomIn=button==replayZoomInButton;
  const lv_color_t color=(zoomIn?replayCanZoomIn():replayCanZoomOut())?lv_color_white():cyd_ui::mutedBorder();
  lv_area_t a; lv_obj_get_coords(button,&a);
  const int cx=(a.x1+a.x2)/2-3, cy=(a.y1+a.y2)/2-3;  // the lens; its handle runs down and to the right
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(event);
  lv_draw_rect_dsc_t ring; lv_draw_rect_dsc_init(&ring);
  ring.radius=LV_RADIUS_CIRCLE; ring.bg_opa=LV_OPA_TRANSP; ring.border_color=color; ring.border_width=2;
  const lv_area_t lens={lv_coord_t(cx-6),lv_coord_t(cy-6),lv_coord_t(cx+6),lv_coord_t(cy+6)};
  lv_draw_rect(ctx,&ring,&lens);
  lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
  line.color=color; line.width=2; line.round_start=true; line.round_end=true;
  lv_point_t from={lv_coord_t(cx+5),lv_coord_t(cy+5)}, to={lv_coord_t(cx+10),lv_coord_t(cy+10)};
  lv_draw_line(ctx,&line,&from,&to);
  line.round_start=false; line.round_end=false;
  from={lv_coord_t(cx-3),lv_coord_t(cy)}; to={lv_coord_t(cx+3),lv_coord_t(cy)};
  lv_draw_line(ctx,&line,&from,&to);
  if(zoomIn) {
    from={lv_coord_t(cx),lv_coord_t(cy-3)}; to={lv_coord_t(cx),lv_coord_t(cy+3)};
    lv_draw_line(ctx,&line,&from,&to);
  }
}
static void replaySetZoomButton(lv_obj_t *button,bool enabled) {
  // A button with nowhere to go is not just dim: it leaves taps alone, with no pressed flash.
  if(enabled) lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE); else lv_obj_clear_flag(button,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_invalidate(button);
}
static void replayUpdateControls() {
  static bool lastPlaying=false;
  if(lastPlaying!=replayPlaying) lv_obj_invalidate(replayPlayButton);
  lastPlaying=replayPlaying;
  // Setting a label invalidates it even when the text is the same, and a tick
  // that changed nothing should not paint anything.
  static uint8_t shownRate=0;
  if(shownRate!=replayRate) {
    char rate[8]; snprintf(rate,sizeof(rate),"%ux",replayRate);
    lv_label_set_text(lv_obj_get_child(replayRateButton,0),rate);
    shownRate=replayRate;
  }
  static uint32_t shownStep=0;
  if(shownStep!=replaySkipStep) {
    char skip[12];
    snprintf(skip,sizeof(skip),"-%lu s",(unsigned long)(replaySkipStep/1000));
    lv_label_set_text(lv_obj_get_child(replayBackButton,0),skip);
    snprintf(skip,sizeof(skip),"+%lu s",(unsigned long)(replaySkipStep/1000));
    lv_label_set_text(lv_obj_get_child(replayForwardButton,0),skip);
    shownStep=replaySkipStep;
  }
  const int canIn=replayCanZoomIn()?1:0, canOut=replayCanZoomOut()?1:0;
  if(replayZoomInShown!=canIn) { replayZoomInShown=int8_t(canIn); replaySetZoomButton(replayZoomInButton,canIn); }
  if(replayZoomOutShown!=canOut) { replayZoomOutShown=int8_t(canOut); replaySetZoomButton(replayZoomOutButton,canOut); }
}
// Scales come from the whole ride's extremes, per field, once per load.
static void replayComputeScales() {
  for(unsigned f=0;f<kFields;f++) {
    const float low=replayGraph->low[f], high=replayGraph->high[f];
    replayLow[f]=replayIncludesZero(f)?LV_MIN(0.0f,floorf(low)):floorf(low);
    replayHigh[f]=LV_MAX(replayLow[f]+1.0f,
                         replayIncludesZero(f)?LV_MAX(0.0f,ceilf(high)):ceilf(high));
  }
}
// ── Zoom: taking a window, the animation, and turning the page ───────────────
// Takes the window the reader has built for the view, if it is ready.
static void replayAdoptDetail() {
  if(!replayViewRequest || replayStatus.windowId!=replayViewRequest) return;
  if(!replayDetail) replayDetail=new(std::nothrow) ride_replay::Window;
  replayDetailValid=replayDetail && rideReplayWindow(*replayDetail);
  replayViewRequest=0;
}
// One frame: the view between the first and last, eased. The span changes by a
// constant factor per frame, so the zoom keeps one speed; the cursor's moment
// keeps its place on screen, as far as the ends of the ride allow.
static void replayZoomFrame(void *,int32_t phase) {
  replayZoomPhase=phase;
  // Single precision: a millisecond is exact to over four hours of ride, and the
  // chip has no double-precision unit.
  const float t=float(phase)/1000.0f;
  const float span0=float(replayZoomFrom[1])-float(replayZoomFrom[0]), span1=float(replayZoomTo[1])-float(replayZoomTo[0]);
  const float span=span0>0&&span1>0?span0*powf(span1/span0,t):span1;
  const float fraction=replayZoomFraction[0]+(replayZoomFraction[1]-replayZoomFraction[0])*t;
  const float duration=float(replayDuration());
  const float start=LV_MAX(0.0f,LV_MIN(float(replayZoomAnchor)-fraction*span,duration-span));
  replayViewStart=uint32_t(lroundf(start));
  replayViewEnd=LV_MIN(uint32_t(lroundf(start+span)),replayDuration());
  prepareReplayGraph();
  replayInvalidateChart();
}
static void replayZoomDone(lv_anim_t *) {
  replayZooming=false;
  replayViewStart=replayZoomTo[0]; replayViewEnd=replayZoomTo[1];
  if(replayZoom==0) replayDetailValid=false;  // the whole ride is the overview's own columns
  replayAdoptDetail();
  prepareReplayGraph();
  replayInvalidateChart();
  replayUpdateControls();
}
static void replayStartZoom(uint32_t start,uint32_t end,uint32_t anchor) {
  lv_anim_del(&replayZoomPhase,nullptr);
  replayZoomFrom[0]=replayViewStart; replayZoomFrom[1]=replayViewEnd;
  replayZoomTo[0]=start; replayZoomTo[1]=end; replayZoomAnchor=anchor;
  const uint32_t *views[2]={replayZoomFrom,replayZoomTo};
  for(int i=0;i<2;i++) {
    const float width=float(LV_MAX(1U,views[i][1]-views[i][0]));
    replayZoomFraction[i]=LV_MAX(0.0f,LV_MIN(1.0f,(float(anchor)-float(views[i][0]))/width));
  }
  // The window for the new view is built while the animation runs. The whole
  // ride needs none, and a window being built for a view we are leaving is dropped.
  if(replayZoom>0) replayViewRequest=rideReplayRequestWindow(start,end,replayLow,replayHigh);
  else { rideReplayRequestWindow(0,0,nullptr,nullptr); replayViewRequest=0; }
  replayNextRequest=0;
  replayZooming=true;
  lv_anim_t anim; lv_anim_init(&anim);
  lv_anim_set_var(&anim,&replayZoomPhase);
  lv_anim_set_exec_cb(&anim,replayZoomFrame);
  lv_anim_set_ready_cb(&anim,replayZoomDone);
  lv_anim_set_values(&anim,0,1000);
  lv_anim_set_time(&anim,kReplayZoomMs);
  lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);
  lv_anim_start(&anim);
  replayUpdateControls();
}
// Where a view of `level` sits: the cursor's moment keeps its place on screen,
// kept off the very edges so there is ride on both sides of it, and the view
// stays inside the ride.
static void replayViewFor(uint8_t level,uint32_t anchor,uint32_t &start,uint32_t &span) {
  const uint32_t duration=replayDuration();
  span=level==0?duration:duration>>level;
  const float fraction=LV_MAX(0.15f,LV_MIN(0.85f,(float(anchor)-float(replayViewStart))/float(replayViewSpan())));
  const int64_t wanted=int64_t(anchor)-int64_t(fraction*float(span));
  start=uint32_t(LV_MAX(int64_t(0),LV_MIN(wanted,int64_t(duration)-int64_t(span))));
}
static void replayZoomBy(int delta) {
  if(!replayHasGraph) return;
  const int level=int(replayZoom)+delta;
  if(level<0 || level>int(replayMaxZoom())) return;
  const uint32_t anchor=replayPlaying?replayStatus.position:replayPosition;
  uint32_t start,span;
  replayViewFor(uint8_t(level),anchor,start,span);
  replayZoom=uint8_t(level);
  replayStartZoom(start,start+span,anchor);
}
// While zoomed the chart is a window, and the cursor leaving it turns the page:
// the next stretch of the same width, with the cursor a tenth of the way in (or
// nine tenths, going back). Nearing the end the next page is built ahead, so its
// columns are usually there when the cursor arrives. True when the view moved.
static bool replayFollowCursor() {
  if(replayZoom==0 || replayZooming || !replayHasGraph) return false;
  const uint32_t cursor=replayPlaying?replayStatus.position:replayPosition;
  const uint32_t span=replayViewSpan(), duration=replayDuration();
  if(cursor>=replayViewStart && cursor<=replayViewEnd) {
    if(!replayViewRequest && !replayNextRequest && replayViewEnd<duration && cursor-replayViewStart>=span/10*6) {
      replayNextStart=LV_MIN(replayViewEnd-span/10,duration-span);
      replayNextEnd=replayNextStart+span;
      replayNextRequest=rideReplayRequestWindow(replayNextStart,replayNextEnd,replayLow,replayHigh);
    }
    return false;
  }
  const bool forward=cursor>replayViewEnd;
  uint32_t start;
  if(forward && replayNextRequest && replayStatus.windowId==replayNextRequest &&
     cursor>=replayNextStart && cursor<=replayNextEnd) {
    // The reader's buffer holds exactly this page: no stretched overview in between.
    start=replayNextStart;
    if(!replayDetail) replayDetail=new(std::nothrow) ride_replay::Window;
    replayDetailValid=replayDetail && rideReplayWindow(*replayDetail);
    replayViewRequest=0;
  } else {
    const uint32_t before=forward?span/10:span-span/10;
    start=LV_MIN(cursor>before?cursor-before:0,duration-span);
    replayViewRequest=rideReplayRequestWindow(start,start+span,replayLow,replayHigh);
  }
  replayNextRequest=0;
  replayViewStart=start; replayViewEnd=start+span;
  return true;
}
static void replayTick(lv_timer_t *) {
  const auto oldStatus=replayStatus;
  const bool hadGraph=replayHasGraph;
  uint32_t now=millis(),delta=now-replayClock; replayClock=now;
  replayStatus=rideReplayStatus();
  if(!rideLoggerStatus().cardReady) { replayPlaying=false; replayStatus.state=RideReplayStatus::Failed; rideReplayClose(); }
  if(replayStatus.state==RideReplayStatus::Ready && !replayHasGraph && replayGraph && rideReplayOverview(*replayGraph)) {
    replayComputeScales();
    replayZoom=0; replayViewStart=0; replayViewEnd=replayStatus.duration;
    prepareReplayGraph();
    replayHasGraph=true;
  }
  if(replayStatus.state!=RideReplayStatus::Ready) replayPlaying=false;
  if(replayPlaying) {
    replayPosition=uint32_t(LV_MIN(uint64_t(replayStatus.duration),uint64_t(replayPosition)+uint64_t(delta)*replayRate));
    rideReplaySeek(replayPosition);
    if(replayPosition==replayStatus.duration) replayPlaying=false;
  }
  // A window the reader has finished takes over from the stretched overview
  // (not mid-zoom: the frames in flight would change source halfway), and the
  // cursor running off the window turns the page.
  bool viewChanged=false;
  if(replayHasGraph && replayStatus.state==RideReplayStatus::Ready) {
    if(!replayZooming && replayViewRequest && replayStatus.windowId==replayViewRequest) { replayAdoptDetail(); viewChanged=true; }
    if(replayFollowCursor()) viewChanged=true;
    if(viewChanged) prepareReplayGraph();
  }
  char elapsed[16],total[16],line[48];
  formatRideDuration(elapsed,sizeof(elapsed),replayPosition/1000);
  formatRideDuration(total,sizeof(total),replayStatus.duration/1000);
  snprintf(line,sizeof(line),"%s / %s%s",elapsed,total,replayStatus.badRecords?txt(" | gaps"," | aukkoja"," | Lücken"," | lacunes"," | huecos"," | lacune"):"");
  if(strcmp(lv_label_get_text(replayTime),line)!=0) lv_label_set_text(replayTime,line);
  if(replaySkipStep!=kReplaySkipShort && now-replayLastSkipTap>kReplayLongSkipHoldMs) {
    replaySkipStep=kReplaySkipShort; replaySkipTaps=0;
  }
  replayUpdateControls();
  // Only a different chart needs all of it repainted: another layout, another
  // view or the ride arriving. Playback and seeking move the cursor, its dots
  // and its bubbles, and repaint just those.
  if(oldStatus.state!=replayStatus.state || hadGraph!=replayHasGraph || viewChanged) replayInvalidateChart();
  else replayInvalidateOverlay();
}
// Seeking never changes whether the ride is playing: a tap on the chart, like
// a skip, moves the moment being watched and leaves the transport alone. Only
// the play button starts and stops playback.
static void replaySeekTo(uint32_t time) {
  replayPosition=LV_MIN(time,replayStatus.duration); rideReplaySeek(replayPosition);
  replayTick(nullptr);
}
static void replayTouch(lv_event_t *event) {
  if(replayStatus.state!=RideReplayStatus::Ready || replayZooming) return;
  if(lv_event_get_code(event)!=LV_EVENT_PRESSED && lv_event_get_code(event)!=LV_EVENT_PRESSING) return;
  lv_indev_t *indev=lv_indev_get_act(); if(!indev) return;
  lv_point_t point; lv_indev_get_point(indev,&point);
  lv_area_t a; lv_obj_get_coords(replayChart,&a);
  int x=LV_MAX(0,LV_MIN(kReplayPlotWidth-1,point.x-a.x1-kReplayPlotX));
  replaySeekTo(replayViewStart+uint32_t(uint64_t(x)*replayViewSpan()/(kReplayPlotWidth-1)));
}
static void replaySkip(int id) {
  uint32_t now=millis();
  if(id!=replayLastSkipId || now-replayLastSkipTap>kReplayQuickTapMs) replaySkipTaps=0;
  replayLastSkipId=id; replayLastSkipTap=now;
  // This tap still uses the current step; the third quick tap widens the next ones.
  uint32_t step=replaySkipStep;
  if(replaySkipTaps<3 && ++replaySkipTaps==3) replaySkipStep=kReplaySkipLong;
  if(id==kReplayBack) replaySeekTo(replayPosition>step?replayPosition-step:0);
  else replaySeekTo(uint32_t(LV_MIN(uint64_t(replayStatus.duration),uint64_t(replayPosition)+step)));
}

// ── Chart layout, remembered across rides ─────────────────────────────────────
static void loadReplayLayout() {
  Preferences prefs;
  uint8_t count=kReplayDefaultCount, fields[kMaxCharts];
  memcpy(fields,kReplayDefaultFields,sizeof(fields));
  if(prefs.begin("replay",true)) {
    count=prefs.getUChar("count",kReplayDefaultCount);
    if(prefs.getBytesLength("fields")==sizeof(fields)) prefs.getBytes("fields",fields,sizeof(fields));
    prefs.end();
  }
  bool valid=count>=1 && count<=kMaxCharts;
  uint8_t seen=0;
  for(uint8_t f:fields) { valid=valid && f<kFields && !(seen&(1U<<f)); if(f<kFields) seen|=1U<<f; }
  if(!valid) { count=kReplayDefaultCount; memcpy(fields,kReplayDefaultFields,sizeof(fields)); }
  replayChartCount=count; memcpy(replayChartField,fields,sizeof(fields));
}
static void saveReplayLayout() {
  Preferences prefs;
  if(!prefs.begin("replay",false)) return;
  if(prefs.getUChar("count",0)!=replayChartCount) prefs.putUChar("count",replayChartCount);
  uint8_t stored[kMaxCharts]={};
  if(prefs.getBytesLength("fields")!=sizeof(stored) || prefs.getBytes("fields",stored,sizeof(stored))!=sizeof(stored) ||
     memcmp(stored,replayChartField,sizeof(stored))!=0)
    prefs.putBytes("fields",replayChartField,sizeof(replayChartField));
  prefs.end();
}
static void replayLayoutChanged() {
  prepareReplayGraph();
  replayInvalidateChart();
  lv_obj_invalidate(replayChartsButton);
}

// ── Popups: ride summary and chart picker ─────────────────────────────────────
static void replayClosePopup() {
  if(!replayPopup) return;
  lv_obj_t *popup=replayPopup;
  replayPopup=nullptr; replaySlotStack=nullptr; memset(replayCountButtons,0,sizeof(replayCountButtons));
  replayPopupKind=ReplayPopup::None;
  // Synchronous, like closeDeveloperPrompt(): LVGL tolerates deleting the
  // clicked object from its own handler, whereas a deferred delete would run
  // on freed memory if a screen rebuild deleted the popup first.
  lv_obj_del(popup);
}
static void replayScrimClicked(lv_event_t *) { replayClosePopup(); }
// Dimmed full-screen scrim with the standard accent-bordered housing. Opening
// one pauses playback so the chart is not redrawn under the translucent scrim.
static lv_obj_t *replayOpenPopup(int y,int h,bool tapAnywhereCloses,int x=6,int w=308) {
  replayPlaying=false;
  replayUpdateControls();
  replayPopup=lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(replayPopup);
  lv_obj_set_pos(replayPopup,0,0);
  lv_obj_set_size(replayPopup,cyd_ui::kScreenWidth,cyd_ui::kScreenHeight);
  lv_obj_set_style_bg_color(replayPopup,lv_color_black(),0);
  lv_obj_set_style_bg_opa(replayPopup,LV_OPA_70,0);
  lv_obj_add_flag(replayPopup,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(replayPopup,LV_OBJ_FLAG_SCROLLABLE);
  if(tapAnywhereCloses) lv_obj_add_event_cb(replayPopup,replayScrimClicked,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *panel=makePanel(replayPopup,x,y,w,h,cyd_ui::kPanelRadius,cyd_ui::chromeAccent(),cyd_ui::panelSurface(),true);
  lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}
static lv_obj_t *replayPopupHeading(lv_obj_t *panel,const char *text,const char *button=nullptr) {
  lv_obj_t *heading=makeLabelAt(panel,10,10,text,lv_color_white(),&lv_font_rajdhani_14,0);
  lv_obj_set_width(heading,200);
  makePopupButton(panel,308-8-76,6,76,30,button?button:txt("DONE","VALMIS","FERTIG","OK","LISTO","FATTO"),false,kReplayPopupDone);
  return heading;
}

static void showReplaySummary() {
  if(replayPopup || !replayGraph || !replayHasGraph) return;
  lv_obj_t *panel=replayOpenPopup(8,224,true);
  replayPopupKind=ReplayPopup::Summary;
  char heading[48];
  snprintf(heading,sizeof(heading),"%s %lu",txt("RIDE SUMMARY","AJON YHTEENVETO","FAHRTÜBERSICHT","RÉSUMÉ DU TRAJET","RESUMEN DEL VIAJE","RIEPILOGO"),
           (unsigned long)replayRideId);
  replayPopupHeading(panel,heading);
  const auto &o=*replayGraph;
  const auto &s=o.summary;
  auto has=[&](uint8_t f){ return (o.fields&(1U<<f))!=0; };
  const float hours=o.duration/3600000.0f;
  char value[13][24];
  for(auto &v:value) snprintf(v,sizeof(v),"--");
  char number[16];
  formatRideDuration(value[0],sizeof(value[0]),o.duration/1000);
  if(s.hasDistance) snprintf(value[1],sizeof(value[1]),"%.1f %s",displayDistance(s.distanceKm),distanceUnitLabel());
  if(s.hasDistance && hours>0) {
    formatSpeedValue(number,sizeof(number),s.distanceKm/hours);
    snprintf(value[2],sizeof(value[2]),"%s %s",number,speedUnitLabel());
  }
  if(has(ride_replay::kSpeed)) { formatSpeedValue(number,sizeof(number),o.high[ride_replay::kSpeed]); snprintf(value[3],sizeof(value[3]),"%s %s",number,speedUnitLabel()); }
  if(has(ride_replay::kBattery)) snprintf(value[4],sizeof(value[4]),"%.0f > %.0f %%",s.first[ride_replay::kBattery],s.last[ride_replay::kBattery]);
  if(has(ride_replay::kVoltage)) snprintf(value[5],sizeof(value[5]),"%.1f V",o.low[ride_replay::kVoltage]);
  if(s.hasEnergy) snprintf(value[7],sizeof(value[7]),"%.0f Wh",s.netWh);
  if(s.hasEnergy && s.hasDistance && s.distanceKm>=0.1f) formatEnergyRate(value[8],sizeof(value[8]),s.netWh/s.distanceKm,true);
  if(s.hasEnergy) snprintf(value[9],sizeof(value[9]),"%.1f Wh",s.regenWh);
  if(has(ride_replay::kPower)) snprintf(value[10],sizeof(value[10]),"%.0f W",o.high[ride_replay::kPower]);
  if(has(ride_replay::kCurrent)) snprintf(value[11],sizeof(value[11]),"%.0f A",o.high[ride_replay::kCurrent]);
  if(has(ride_replay::kMotorCurrent)) snprintf(value[12],sizeof(value[12]),"%.0f A",o.high[ride_replay::kMotorCurrent]);
  if(has(ride_replay::kMotorTemp) || has(ride_replay::kControllerTemp)) {
    char motor[8]="--",controller[8]="--";
    if(has(ride_replay::kMotorTemp)) snprintf(motor,sizeof(motor),"%.0f",o.high[ride_replay::kMotorTemp]);
    if(has(ride_replay::kControllerTemp)) snprintf(controller,sizeof(controller),"%.0f",o.high[ride_replay::kControllerTemp]);
    snprintf(value[6],sizeof(value[6]),"%s / %s °C",motor,controller);
  }
  const char *label[13]={
    txt("Duration","Kesto","Dauer","Durée","Duración","Durata"),
    txt("Distance","Matka","Strecke","Distance","Distancia","Distanza"),
    txt("Avg speed","Keskinopeus","Mittl. Tempo","Vit. moy.","Vel. media","Vel. media"),
    txt("Top speed","Huippunopeus","Höchsttempo","Vit. max","Vel. máx.","Vel. max"),
    txt("Battery","Akku","Akku","Batterie","Batería","Batteria"),
    txt("Min voltage","Min. jännite","Min. Spannung","Tension min","Voltaje mín.","Tensione min"),
    txt("Motor / ctrl","Moott. / ohj.","Motor / Regler","Mot. / contr.","Motor / contr.","Mot. / centr."),
    txt("Energy","Energia","Energie","Énergie","Energía","Energia"),
    txt("Consumption","Kulutus","Verbrauch","Conso.","Consumo","Consumo"),
    txt("Regen","Talteenotto","Rekuperation","Régén.","Regen.","Recupero"),
    txt("Peak power","Huipputeho","Spitzenleistung","Puiss. max","Potencia máx.","Potenza max"),
    txt("Peak current","Huippuvirta","Spitzenstrom","Courant max","Corriente máx.","Corrente max"),
    txt("Peak phase","Huippuvaihevirta","Max. Phasenstrom","Phase max","Fase máx.","Fase max"),
  };
  // Two columns of label-left, value-right rows; left: the ride, right: what
  // it drew. The left column takes the odd row: the right one stops short of
  // the Delete ride button.
  for(int i=0;i<13;i++) {
    const int column=i<7?0:1, row=i<7?i:i-7;
    const int x=10+column*148, y=44+row*22, w=140;
    lv_obj_t *name=makeLabelAt(panel,x,y,label[i],cyd_ui::secondaryText(),&lv_font_rajdhani_12,0);
    lv_obj_set_width(name,w);
    lv_obj_t *shown=makeLabelAt(panel,x+w,y,value[i],lv_color_white(),&lv_font_rajdhani_12,4);
    lv_obj_set_width(shown,90); lv_obj_set_x(shown,x+w-90);
  }
  makePopupButton(panel,308-8-130,224-8-32,130,32,txt("DELETE RIDE","POISTA AJO","FAHRT LÖSCHEN","SUPPRIMER","ELIMINAR","ELIMINA"),
                  false,kReplayDeleteRide);
}
// Compact confirmation over the replay, standing in for the summary until
// Cancel returns to it.
static void showReplayDeleteConfirm() {
  if(replayPopup) return;
  constexpr int kW=236, kH=112;
  lv_obj_t *panel=replayOpenPopup((cyd_ui::kScreenHeight-kH)/2,kH,false,(cyd_ui::kScreenWidth-kW)/2,kW);
  replayPopupKind=ReplayPopup::DeleteConfirm;
  char heading[40];
  snprintf(heading,sizeof(heading),"%s %lu?",txt("DELETE RIDE","POISTA AJO","FAHRT LÖSCHEN","SUPPRIMER LE TRAJET","ELIMINAR VIAJE","ELIMINA VIAGGIO"),
           (unsigned long)replayRideId);
  lv_obj_t *title=makeLabelAt(panel,kW/2,10,heading,lv_color_white(),&lv_font_rajdhani_14,3);
  lv_obj_set_width(title,kW-16); lv_obj_set_x(title,8);
  lv_obj_t *body=makeLabelAt(panel,kW/2,32,txt("This cannot be undone.","Tätä ei voi perua.","Kann nicht rückgängig gemacht werden.",
                                                 "Action irréversible.","No se puede deshacer.","Non si può annullare."),
                             cyd_ui::secondaryText(),&lv_font_rajdhani_12,3);
  lv_obj_set_width(body,kW-16); lv_obj_set_x(body,8);
  const int buttonW=(kW-16-cyd_ui::kControlGap)/2;
  makePopupButton(panel,8,kH-8-34,buttonW,34,txt("CANCEL","PERUUTA","ABBRECHEN","ANNULER","CANCELAR","ANNULLA"),false,kReplayPopupDone);
  makePopupButton(panel,8+buttonW+cyd_ui::kControlGap,kH-8-34,buttonW,34,txt("DELETE","POISTA","LÖSCHEN","SUPPRIMER","ELIMINAR","ELIMINA"),
                  false,kReplayDeleteConfirm);
}

// A choice outlined and lettered in its field's colour. The current choice is
// filled and carries the thicker selected border, still in the field colour.
static lv_obj_t *makeReplayFieldButton(lv_obj_t *parent,int x,int y,int w,int h,uint8_t f,bool current,int id,
                                       const lv_font_t *font) {
  lv_obj_t *btn=makePopupButton(parent,x,y,w,h,replayFieldName(f),false,id);
  // Fields this ride never recorded stay choosable but read as dimmed.
  const lv_color_t color=replayFieldRecorded(f)?c565(cyd_ui::kReplayTrace565[f]):cyd_ui::mutedBorder();
  lv_obj_set_style_bg_color(btn,current?cyd_ui::activeControlSurface():cyd_ui::controlSurface(),0);
  lv_obj_set_style_border_color(btn,color,0);
  lv_obj_set_style_border_width(btn,current?3:2,0);
  lv_obj_t *text=lv_obj_get_child(btn,0);
  lv_obj_set_style_text_color(text,color,0);
  lv_obj_set_style_text_font(text,font,0);
  return btn;
}
// The chart buttons mirror the real layout: stacked in chart order, splitting
// the stack's height the way the charts split the screen.
static constexpr int kReplayStackX=8, kReplayStackY=82, kReplayStackW=308-16, kReplayStackH=224-82-8;
static void buildReplaySlotStack() {
  lv_obj_clean(replaySlotStack);
  const int gap=4, each=(kReplayStackH-(replayChartCount-1)*gap)/replayChartCount;
  for(int slot=0;slot<replayChartCount;slot++)
    makeReplayFieldButton(replaySlotStack,0,slot*(each+gap),kReplayStackW,each,
                          replayChartField[slot],false,kReplaySlotBase+slot,&lv_font_rajdhani_14);
  // The chosen count uses the standard selected choice: filled, orange border and text.
  for(unsigned i=0;i<kMaxCharts;i++) {
    lv_obj_t *btn=replayCountButtons[i];
    if(!btn) continue;
    const bool active=replayChartCount==i+1;
    lv_obj_set_style_bg_color(btn,active?cyd_ui::activeControlSurface():cyd_ui::controlSurface(),0);
    lv_obj_set_style_border_width(btn,active?3:cyd_ui::kBorderWidth,0);
    lv_obj_set_style_border_color(btn,active?cyd_ui::chromeAccent():cyd_ui::idleControlBorder(),0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn,0),active?cyd_ui::chromeAccent():lv_color_white(),0);
  }
}
static void showReplayChartPicker() {
  if(replayPopup) return;
  lv_obj_t *panel=replayOpenPopup(8,224,false);
  replayPopupKind=ReplayPopup::Charts;
  replayPopupHeading(panel,txt("CHARTS","KAAVIOT","DIAGRAMME","GRAPHIQUES","GRÁFICOS","GRAFICI"));
  // Reset sits beside Done: back to speed, power and voltage.
  makePopupButton(panel,308-8-76-cyd_ui::kControlGap-76,6,76,30,
                  txt("RESET","NOLLAA","ZURÜCKS.","RÉINIT.","RESTABL.","RIPRIST."),false,kReplayChartsReset);
  // "Number of charts: [1] [2] [3] [4]", all on one row.
  const char *prompt=txt("Number of charts:","Kaavioiden määrä:","Anzahl Diagramme:","Nombre de graphiques :","Número de gráficos:","Numero di grafici:");
  lv_obj_t *label=makeLabelAt(panel,10,51,prompt,cyd_ui::secondaryText(),&lv_font_rajdhani_12,0);
  lv_point_t size; lv_txt_get_size(&size,prompt,&lv_font_rajdhani_12,0,0,220,LV_TEXT_FLAG_NONE);
  lv_obj_set_width(label,size.x+4);
  const int countX=10+size.x+10, countRoom=308-8-countX, countGap=cyd_ui::kControlGap;
  const int countW=LV_MIN(44,(countRoom-int(kMaxCharts-1)*countGap)/int(kMaxCharts));
  static const char *const counts[kMaxCharts]={"1","2","3","4"};
  for(unsigned i=0;i<kMaxCharts;i++)
    replayCountButtons[i]=makePopupButton(panel,countX+i*(countW+countGap),44,countW,30,counts[i],false,kReplayCountBase+i);
  replaySlotStack=lv_obj_create(panel);
  lv_obj_remove_style_all(replaySlotStack);
  lv_obj_set_pos(replaySlotStack,kReplayStackX,kReplayStackY);
  lv_obj_set_size(replaySlotStack,kReplayStackW,kReplayStackH);
  lv_obj_clear_flag(replaySlotStack,LV_OBJ_FLAG_SCROLLABLE);
  buildReplaySlotStack();
}
// Every field for one chart, as a two-column grid.
static void showReplayFieldList() {
  if(replayPopup) return;
  lv_obj_t *panel=replayOpenPopup(8,224,false);
  replayPopupKind=ReplayPopup::Fields;
  char heading[32];
  snprintf(heading,sizeof(heading),"%s %u",txt("CHART","KAAVIO","DIAGRAMM","GRAPHIQUE","GRÁFICO","GRAFICO"),replayEditSlot+1);
  replayPopupHeading(panel,heading,txt("BACK","TAKAISIN","ZURÜCK","RETOUR","ATRÁS","INDIETRO"));
  for(unsigned f=0;f<kFields;f++)
    makeReplayFieldButton(panel,8+(f%2)*149,44+(f/2)*44,143,38,f,replayChartField[replayEditSlot]==f,
                          kReplayFieldBase+f,&lv_font_rajdhani_12);
}
static void replayPopupAction(int id) {
  switch(replayPopupKind) {
    case ReplayPopup::Summary:
      if(id==kReplayPopupDone) replayClosePopup();
      else if(id==kReplayDeleteRide) { replayClosePopup(); showReplayDeleteConfirm(); }
      break;
    case ReplayPopup::DeleteConfirm:
      replayClosePopup();
      if(id==kReplayDeleteConfirm) {
        // Stop reading the file before the writer removes it, then return to
        // the list, whose catalog drops the ride as soon as it is gone.
        rideReplayClose();
        rideLoggerDeleteRide(replayRideId);
        queueLowMemoryRebuild(SCREEN_RIDE_LOGS);
      } else showReplaySummary();
      break;
    case ReplayPopup::Charts:
      if(id==kReplayPopupDone) { saveReplayLayout(); replayClosePopup(); }
      else if(id>=kReplayCountBase && id<kReplayCountBase+int(kMaxCharts)) {
        replayChartCount=id-kReplayCountBase+1;
        replayLayoutChanged(); buildReplaySlotStack();
      } else if(id==kReplayChartsReset) {
        // Applied live like any other change; Done stores it.
        replayChartCount=kReplayDefaultCount;
        memcpy(replayChartField,kReplayDefaultFields,sizeof(replayChartField));
        replayLayoutChanged(); buildReplaySlotStack();
      } else if(id>=kReplaySlotBase && id<kReplaySlotBase+int(kMaxCharts)) {
        replayEditSlot=id-kReplaySlotBase;
        replayClosePopup(); showReplayFieldList();
      }
      break;
    case ReplayPopup::Fields:
      if(id>=kReplayFieldBase && id<kReplayFieldBase+int(kFields)) {
        const uint8_t chosen=id-kReplayFieldBase;
        // A field another chart already uses swaps with this chart's, so all
        // four slots stay distinct.
        for(unsigned slot=0;slot<kMaxCharts;slot++)
          if(slot!=replayEditSlot && replayChartField[slot]==chosen) replayChartField[slot]=replayChartField[replayEditSlot];
        replayChartField[replayEditSlot]=chosen;
        replayLayoutChanged();
      } else if(id!=kReplayPopupDone) break;
      replayClosePopup(); showReplayChartPicker();
      break;
    default: break;
  }
}

static void replayAction(int id) {
  if(replayPopup) { replayPopupAction(id); return; }
  if(id==BTN_BACK) { queueLowMemoryRebuild(SCREEN_RIDE_LOGS); return; }
  if(replayStatus.state!=RideReplayStatus::Ready) return;
  if(id==kReplaySummary) { showReplaySummary(); return; }
  if(id==kReplayCharts) { showReplayChartPicker(); return; }
  if(id==kReplayPlay) {
    if(replayPosition>=replayStatus.duration) { replayPosition=0; rideReplaySeek(0); }
    replayPlaying=!replayPlaying; replayClock=millis();
  } else if(id==kReplayBack || id==kReplayForward) replaySkip(id);
  else if(id==kReplayZoomIn || id==kReplayZoomOut) replayZoomBy(id==kReplayZoomIn?1:-1);
  else if(id==kReplayRate) {
    size_t next=0;
    while(next<sizeof(kReplayRates) && kReplayRates[next]!=replayRate) next++;
    replayRate=kReplayRates[(next+1)%sizeof(kReplayRates)];
  }
  replayUpdateControls(); replayInvalidateOverlay();
}
static void closeReplayScreen() {
  if(replayTimer) { lv_timer_del(replayTimer); replayTimer=nullptr; }
  lv_anim_del(&replayZoomPhase,nullptr);
  replayZooming=false; replayZoom=0; replayViewRequest=replayNextRequest=0; replayDetailValid=false;
  delete replayDetail; replayDetail=nullptr;
  replayZoomOutButton=replayZoomInButton=nullptr;
  rideReplayClose(); delete replayGraph; replayGraph=nullptr;
  replayChart=nullptr; replayChartsButton=nullptr; replayPopup=nullptr; replaySlotStack=nullptr;
  memset(replayCountButtons,0,sizeof(replayCountButtons));
  replayPopupKind=ReplayPopup::None; replayPlaying=false;
}
// The title doubles as the Summary button: "REPLAY RIDE 12" over the
// elapsed/total time. Longer translations step down a size, then drop the
// word "RIDE", rather than clip.
static void makeReplayTitleButton(lv_obj_t *scr,int x,int w) {
  lv_obj_t *button=makeNavButton(scr,x,cyd_ui::kTopBarY,"",kReplaySummary,cyd_ui::kTopBarHeight);
  lv_obj_set_width(button,w);
  const char *heading=txt("REPLAY","TOISTO","WIEDERGABE","RELECTURE","REPETICIÓN","RIPRODUCI");
  char title[24];
  const int gap=5, room=w-10;
  const lv_font_t *font=&lv_font_rajdhani_14;
  lv_point_t headingSize,titleSize;
  for(int attempt=0;;attempt++) {
    if(attempt<2) snprintf(title,sizeof(title),"%s %lu",txt("RIDE","AJO","FAHRT","TRAJET","VIAJE","VIAGGIO"),(unsigned long)replayRideId);
    else snprintf(title,sizeof(title),"%lu",(unsigned long)replayRideId);
    font=attempt==0?&lv_font_rajdhani_14:&lv_font_rajdhani_12;
    lv_txt_get_size(&headingSize,heading,font,0,0,200,LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&titleSize,title,font,0,0,200,LV_TEXT_FLAG_NONE);
    if(headingSize.x+gap+titleSize.x<=room || attempt==2) break;
  }
  const int left=w/2-(headingSize.x+gap+titleSize.x)/2, y=font==&lv_font_rajdhani_14?2:3;
  makeLabelAt(button,left+headingSize.x,y,heading,cyd_ui::chromeAccent(),font,4);
  makeLabelAt(button,left+headingSize.x+gap,y,title,lv_color_white(),font,0);
  replayTime=makeLabelAt(button,w/2,19,"",cyd_ui::secondaryText(),&lv_font_rajdhani_12,3);
}
static void showRideReplay() {
  lv_obj_t *scr=makeScreen(); currentAction=replayAction;
  replayPlaying=false; replayPosition=0; replayRate=1; replayHasGraph=false; replayClock=millis();
  replayShown=ReplayOverlay();
  replayZoom=0; replayViewStart=replayViewEnd=0; replayDetailValid=false; replayViewRequest=replayNextRequest=0;
  replayZooming=false; replayZoomInShown=replayZoomOutShown=-1;
  replaySkipStep=kReplaySkipShort; replaySkipTaps=0; replayLastSkipId=0;
  replayPopup=nullptr; replaySlotStack=nullptr; memset(replayCountButtons,0,sizeof(replayCountButtons));
  replayPopupKind=ReplayPopup::None; replayEditSlot=0;
  loadReplayLayout();
  replayGraph=new(std::nothrow) ride_replay::Overview;
  replayOpenFailed=!replayGraph || !rideReplayOpen(replayRideId);
  // Back | title (Summary) | Charts | rate, with the standard control gap.
  constexpr int kSmallTop=44, kRateX=cyd_ui::kScreenWidth-cyd_ui::kEdgeInset-kSmallTop;
  constexpr int kChartsX=kRateX-cyd_ui::kControlGap-kSmallTop;
  constexpr int kTitleX=cyd_ui::kEdgeInset+cyd_ui::kTopBarWidth+cyd_ui::kControlGap;
  auto *back=makeNavButton(scr,cyd_ui::kEdgeInset,cyd_ui::kTopBarY,txt("< BACK","< TAKAISIN","< ZURÜCK","< RETOUR","< ATRÁS","< INDIETRO"),BTN_BACK,cyd_ui::kTopBarHeight);
  lv_obj_set_width(back,cyd_ui::kTopBarWidth);
  makeReplayTitleButton(scr,kTitleX,kChartsX-cyd_ui::kControlGap-kTitleX);
  replayChartsButton=makeNavButton(scr,kChartsX,cyd_ui::kTopBarY,"",kReplayCharts,cyd_ui::kTopBarHeight);
  lv_obj_set_width(replayChartsButton,kSmallTop);
  lv_obj_add_event_cb(replayChartsButton,replayChartsGlyphDraw,LV_EVENT_DRAW_MAIN_END,nullptr);
  replayRateButton=makeNavButton(scr,kRateX,cyd_ui::kTopBarY,"1x",kReplayRate,cyd_ui::kTopBarHeight);
  lv_obj_set_width(replayRateButton,kSmallTop);
  replayChart=lv_obj_create(scr); lv_obj_remove_style_all(replayChart);
  lv_obj_set_pos(replayChart,0,kReplayChartY); lv_obj_set_size(replayChart,320,kReplayChartArea+kReplayStripRows);
  lv_obj_clear_flag(replayChart,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(replayChart,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(replayChart,replayDraw,LV_EVENT_DRAW_MAIN,nullptr);
  lv_obj_add_event_cb(replayChart,replayTouch,LV_EVENT_PRESSED,nullptr);
  lv_obj_add_event_cb(replayChart,replayTouch,LV_EVENT_PRESSING,nullptr);
  // Bottom row: zoom out | -10 s | play | +10 s | zoom in, with the standard control gap.
  constexpr int kBottomY=204, kBottomHeight=34, kZoomWidth=34, kSkipWidth=70, kPlayWidth=88, kGap=cyd_ui::kControlGap;
  constexpr int kZoomOutX=cyd_ui::kEdgeInset, kSkipBackX=kZoomOutX+kZoomWidth+kGap, kPlayX=kSkipBackX+kSkipWidth+kGap;
  constexpr int kSkipForwardX=kPlayX+kPlayWidth+kGap, kZoomInX=kSkipForwardX+kSkipWidth+kGap;
  static_assert(kZoomInX+kZoomWidth+cyd_ui::kEdgeInset==cyd_ui::kScreenWidth,"the bottom row spans the screen");
  replayZoomOutButton=makeNavButton(scr,kZoomOutX,kBottomY,"",kReplayZoomOut,kBottomHeight); lv_obj_set_width(replayZoomOutButton,kZoomWidth);
  lv_obj_add_event_cb(replayZoomOutButton,replayZoomGlyphDraw,LV_EVENT_DRAW_MAIN_END,nullptr);
  replayBackButton=makeNavButton(scr,kSkipBackX,kBottomY,"-10 s",kReplayBack,kBottomHeight); lv_obj_set_width(replayBackButton,kSkipWidth);
  replayPlayButton=makeNavButton(scr,kPlayX,kBottomY,"",kReplayPlay,kBottomHeight); lv_obj_set_width(replayPlayButton,kPlayWidth);
  lv_obj_add_event_cb(replayPlayButton,replayTransportDraw,LV_EVENT_DRAW_MAIN_END,nullptr);
  replayForwardButton=makeNavButton(scr,kSkipForwardX,kBottomY,"+10 s",kReplayForward,kBottomHeight); lv_obj_set_width(replayForwardButton,kSkipWidth);
  replayZoomInButton=makeNavButton(scr,kZoomInX,kBottomY,"",kReplayZoomIn,kBottomHeight); lv_obj_set_width(replayZoomInButton,kZoomWidth);
  lv_obj_add_event_cb(replayZoomInButton,replayZoomGlyphDraw,LV_EVENT_DRAW_MAIN_END,nullptr);
  loadScreen(scr); replayTick(nullptr);
  replayTimer=lv_timer_create(replayTick,100,nullptr);
}
#ifdef CYD_LVGL_PREVIEW
void uiPreviewRideReplay(uint32_t position,bool playing) {
  replayRideId=12; uiShow(SCREEN_RIDE_REPLAY); replaySeekTo(position); replayPlaying=playing; replayUpdateControls();
  replayInvalidateOverlay();
}
// Jumps to a zoom level without the animation, about the cursor, and takes the
// window the host has already built.
void uiPreviewRideReplayZoom(uint8_t level) {
  if(!replayHasGraph) return;
  level=LV_MIN(level,replayMaxZoom());
  const uint32_t anchor=replayPlaying?replayStatus.position:replayPosition;
  uint32_t start,span;
  replayViewFor(level,anchor,start,span);
  lv_anim_del(&replayZoomPhase,nullptr);
  replayZooming=false; replayZoom=level; replayNextRequest=0; replayDetailValid=false;
  replayViewStart=start; replayViewEnd=start+span;
  replayViewRequest=level?rideReplayRequestWindow(start,start+span,replayLow,replayHigh):0;
  if(!level) rideReplayRequestWindow(0,0,nullptr,nullptr);
  replayStatus=rideReplayStatus();
  replayAdoptDetail();
  prepareReplayGraph(); replayInvalidateChart(); replayUpdateControls();
}
// The zoom level, the stretch of ride on screen, and whether the reader's own
// columns for it have arrived (otherwise it is the overview, stretched).
void uiPreviewReplayView(uint8_t &zoom,uint32_t &start,uint32_t &end,bool &detail) {
  zoom=replayZoom; start=replayViewStart; end=replayViewEnd; detail=replayDetailValid;
}
// Layout and popups for captures. Leaves the stored layout untouched.
void uiPreviewRideReplayLayout(uint8_t count,const uint8_t fields[4]) {
  replayChartCount=count; memcpy(replayChartField,fields,kMaxCharts); replayLayoutChanged();
}
// 0 summary, 1 chart layout, 2 the field list for the second chart,
// 3 the delete confirmation.
void uiPreviewRideReplayPopup(uint8_t popup) {
  if(popup==0) showReplaySummary();
  else if(popup==1) showReplayChartPicker();
  else if(popup==2) { replayEditSlot=1; showReplayFieldList(); }
  else showReplayDeleteConfirm();
}
#endif
