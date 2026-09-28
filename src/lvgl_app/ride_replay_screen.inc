// Included by screens.cpp after the canonical menu constructors. This keeps
// replay-specific state together while reusing the shared navigation buttons.
using ride_replay::kFields;
using ride_replay::kMaxCharts;
static constexpr int kReplayRowBase=12000;
static constexpr int kReplayPlay=12010, kReplayBack=12011, kReplayForward=12012, kReplayRate=12013;
static constexpr int kReplayCharts=12014, kReplaySummary=12015, kReplayPopupDone=12016, kReplayChartsReset=12017;
static constexpr int kReplayDeleteRide=12018, kReplayDeleteConfirm=12019;
static constexpr int kReplayCountBase=12020, kReplaySlotBase=12030, kReplayFieldBase=12040;
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
// Recomputed whenever the chart layout or a chart's field changes.
static uint8_t replayHeights[kMaxCharts][kReplayPlotWidth];
static void prepareReplayGraph() {
  if(!replayGraph) return;
  const int maxLevel=replayMaxLevel();
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    memset(replayHeights[slot],255,kReplayPlotWidth);
    auto level=[&](float v) {
      return uint8_t(lroundf(float(maxLevel)*LV_MAX(0.0f,LV_MIN(1.0f,(v-replayLow[f])/(replayHigh[f]-replayLow[f])))));
    };
    for(int x=0;x<kReplayPlotWidth;x++) {
      const unsigned b=LV_MIN(ride_replay::kBuckets-1,
                              unsigned(x)*ride_replay::kBuckets/(kReplayPlotWidth-1));
      const auto &bucket=replayGraph->buckets[b];
      if(!(bucket.mask&(1<<f)) || (bucket.gaps&(1<<f))) continue;
      replayHeights[slot][x]=level(bucket.low[f]);
    }
  }
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
  const int bandHeight=replayBandHeight(), baseRow=replayBase(), maxLevel=replayMaxLevel();
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    auto color=c565(cyd_ui::kReplayTrace565[f]);
    int top=oy+slot*bandHeight;
    const int base=top+baseRow;
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
    const auto &clip=*ctx->clip_area;
    const int first=LV_MAX(0,clip.x1-ox-kReplayPlotX);
    const int last=LV_MIN(kReplayPlotWidth-1,clip.x2-ox-kReplayPlotX);
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
    // The line and fill share each interval's minimum height.
    int previousX=-1,previousY=0;
    for(int x=0;x<kReplayPlotWidth;x++) {
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
  uint32_t shownPosition=replayPlaying?replayStatus.position:replayPosition;
  const auto positionToX=[&](uint32_t position) {
    return int(ox+kReplayPlotX+uint64_t(position)*(kReplayPlotWidth-1)/LV_MAX(1U,replayStatus.duration));
  };
  int cursor=positionToX(shownPosition);
  // The reader decodes seeks on its own task, so while a finger drags the
  // cursor the newest sample is still a few milliseconds behind it. Draw that
  // sample rather than blanking every bubble to "--" until the drag stops: the
  // dot sits at the moment it belongs to, so the lag is visible but honest.
  const int sampleCursor=positionToX(replayStatus.position);
  // A grey core with dimmer shoulders reads as a cursor without glaring.
  const int cursorBottom=oy+replayChartCount*bandHeight-2;
  replayLine(ctx,cursor-1,oy,cursor-1,cursorBottom,lv_color_hex(0x505050));
  replayLine(ctx,cursor+1,oy,cursor+1,cursorBottom,lv_color_hex(0x505050));
  replayLine(ctx,cursor,oy,cursor,cursorBottom,lv_color_hex(0xB4B4B4));
  // Bubbles sit left of the cursor from a quarter of the way across the plot
  // onward and right of it before, all together. The point is fixed, never
  // derived from the current values, so a bubble that widens as its value
  // changes (power entering regen) cannot make them hop back and forth. A
  // wide bubble just left of it may run over the scale labels.
  const bool bubblesLeft=cursor>=ox+kReplayPlotX+kReplayPlotWidth/4;
  char captions[kMaxCharts][32];
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    const bool valid=replayStatus.sample.mask&(1<<f);
    if(valid) formatReplayValue(captions[slot],sizeof(captions[slot]),f,replayStatus.sample.value[f]);
    else snprintf(captions[slot],sizeof(captions[slot]),"--");
  }
  for(int slot=0;slot<replayChartCount;slot++) {
    const uint8_t f=replayChartField[slot];
    if(!replayFieldRecorded(f)) continue;
    auto color=c565(cyd_ui::kReplayTrace565[f]);
    bool valid=replayStatus.sample.mask&(1<<f);
    if(valid) {
      int pointY=oy+replayY(slot,replayStatus.sample.value[f]);
      lv_draw_rect_dsc_t dot; lv_draw_rect_dsc_init(&dot); dot.radius=LV_RADIUS_CIRCLE;
      dot.bg_color=color; dot.border_color=lv_color_white(); dot.border_width=1;
      lv_area_t a={lv_coord_t(sampleCursor-3),lv_coord_t(pointY-3),lv_coord_t(sampleCursor+3),lv_coord_t(pointY+3)};
      lv_draw_rect(ctx,&dot,&a);
    }
    const char *caption=captions[slot];
    lv_point_t textSize; lv_txt_get_size(&textSize,caption,&lv_font_rajdhani_12,0,0,250,LV_TEXT_FLAG_NONE);
    // Fixed height, centred in the band, so bubbles never jump with the data.
    int w=LV_MIN(250,textSize.x+10);
    int x=bubblesLeft?cursor-kReplayBubbleGap-w:cursor+kReplayBubbleGap;
    // Kept on screen, but free to cover the scale labels on the left.
    x=LV_MAX(ox+1,LV_MIN(ox+316-w,x));
    int y=oy+slot*bandHeight+(bandHeight-1-20)/2;
    lv_draw_rect_dsc_t bubble; lv_draw_rect_dsc_init(&bubble); bubble.radius=4;
    bubble.bg_color=lv_color_black(); bubble.border_color=color; bubble.border_width=1;
    lv_area_t a={lv_coord_t(x),lv_coord_t(y),lv_coord_t(x+w),lv_coord_t(y+19)};
    lv_draw_rect(ctx,&bubble,&a);
    replayText(ctx,x+5,y+2,w-8,caption,lv_color_white());
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
static void replayUpdateControls() {
  static bool lastPlaying=false;
  if(lastPlaying!=replayPlaying) lv_obj_invalidate(replayPlayButton);
  lastPlaying=replayPlaying;
  char rate[8]; snprintf(rate,sizeof(rate),"%ux",replayRate);
  lv_label_set_text(lv_obj_get_child(replayRateButton,0),rate);
  static uint32_t shownStep=0;
  if(shownStep!=replaySkipStep) {
    char skip[12];
    snprintf(skip,sizeof(skip),"-%lu s",(unsigned long)(replaySkipStep/1000));
    lv_label_set_text(lv_obj_get_child(replayBackButton,0),skip);
    snprintf(skip,sizeof(skip),"+%lu s",(unsigned long)(replaySkipStep/1000));
    lv_label_set_text(lv_obj_get_child(replayForwardButton,0),skip);
    shownStep=replaySkipStep;
  }
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
static void replayTick(lv_timer_t *) {
  const auto oldStatus=replayStatus;
  const uint32_t oldPosition=replayPosition;
  const bool hadGraph=replayHasGraph;
  uint32_t now=millis(),delta=now-replayClock; replayClock=now;
  replayStatus=rideReplayStatus();
  if(!rideLoggerStatus().cardReady) { replayPlaying=false; replayStatus.state=RideReplayStatus::Failed; rideReplayClose(); }
  if(replayStatus.state==RideReplayStatus::Ready && !replayHasGraph && replayGraph && rideReplayOverview(*replayGraph)) {
    replayComputeScales();
    prepareReplayGraph();
    replayHasGraph=true;
  }
  if(replayStatus.state!=RideReplayStatus::Ready) replayPlaying=false;
  if(replayPlaying) {
    replayPosition=uint32_t(LV_MIN(uint64_t(replayStatus.duration),uint64_t(replayPosition)+uint64_t(delta)*replayRate));
    rideReplaySeek(replayPosition);
    if(replayPosition==replayStatus.duration) replayPlaying=false;
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
  if(oldStatus.state!=replayStatus.state || oldStatus.position!=replayStatus.position ||
     oldStatus.sample.mask!=replayStatus.sample.mask || oldPosition!=replayPosition || hadGraph!=replayHasGraph)
    lv_obj_invalidate(replayChart);
}
// Seeking never changes whether the ride is playing: a tap on the chart, like
// a skip, moves the moment being watched and leaves the transport alone. Only
// the play button starts and stops playback.
static void replaySeekTo(uint32_t time) {
  replayPosition=LV_MIN(time,replayStatus.duration); rideReplaySeek(replayPosition);
  replayTick(nullptr);
  lv_obj_invalidate(replayChart);
}
static void replayTouch(lv_event_t *event) {
  if(replayStatus.state!=RideReplayStatus::Ready) return;
  if(lv_event_get_code(event)!=LV_EVENT_PRESSED && lv_event_get_code(event)!=LV_EVENT_PRESSING) return;
  lv_indev_t *indev=lv_indev_get_act(); if(!indev) return;
  lv_point_t point; lv_indev_get_point(indev,&point);
  lv_area_t a; lv_obj_get_coords(replayChart,&a);
  int x=LV_MAX(0,LV_MIN(kReplayPlotWidth-1,point.x-a.x1-kReplayPlotX));
  replaySeekTo(uint64_t(x)*replayStatus.duration/(kReplayPlotWidth-1));
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
  lv_obj_invalidate(replayChart);
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
  else if(id==kReplayRate) {
    size_t next=0;
    while(next<sizeof(kReplayRates) && kReplayRates[next]!=replayRate) next++;
    replayRate=kReplayRates[(next+1)%sizeof(kReplayRates)];
  }
  replayUpdateControls(); lv_obj_invalidate(replayChart);
}
static void closeReplayScreen() {
  if(replayTimer) { lv_timer_del(replayTimer); replayTimer=nullptr; }
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
  lv_obj_set_pos(replayChart,0,kReplayChartY); lv_obj_set_size(replayChart,320,kReplayChartArea);
  lv_obj_clear_flag(replayChart,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(replayChart,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(replayChart,replayDraw,LV_EVENT_DRAW_MAIN,nullptr);
  lv_obj_add_event_cb(replayChart,replayTouch,LV_EVENT_PRESSED,nullptr);
  lv_obj_add_event_cb(replayChart,replayTouch,LV_EVENT_PRESSING,nullptr);
  replayBackButton=makeNavButton(scr,cyd_ui::kEdgeInset,204,"-10 s",kReplayBack,34); lv_obj_set_width(replayBackButton,101);
  replayPlayButton=makeNavButton(scr,108,204,"",kReplayPlay,34); lv_obj_set_width(replayPlayButton,104);
  lv_obj_add_event_cb(replayPlayButton,replayTransportDraw,LV_EVENT_DRAW_MAIN_END,nullptr);
  replayForwardButton=makeNavButton(scr,217,204,"+10 s",kReplayForward,34); lv_obj_set_width(replayForwardButton,101);
  loadScreen(scr); replayTick(nullptr);
  replayTimer=lv_timer_create(replayTick,100,nullptr);
}
#ifdef CYD_LVGL_PREVIEW
void uiPreviewRideReplay(uint32_t position,bool playing) {
  replayRideId=12; uiShow(SCREEN_RIDE_REPLAY); replaySeekTo(position); replayPlaying=playing; replayUpdateControls();
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
