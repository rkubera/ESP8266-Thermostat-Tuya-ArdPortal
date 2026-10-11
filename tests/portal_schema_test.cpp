#include "../portal_features.h"
#define ARDPORTAL_DEFINE_HA_DATA
#include "Arduino.h"
struct SchemaHeapMock {uint32_t getFreeHeap(){return 1000000;} uint32_t getMaxFreeBlockSize(){return 1000000;}} ESP;
#include "DynamicPages.h"
#include "../portal_pages.h"
#include "../portal_state_fields.h"
#include <cassert>
#include <iostream>
uint32_t fakeNow=0;
std::function<void(uint32_t)> onAdvance;
void advanceTime(uint32_t delta){fakeNow+=delta;}
using V=ArdJSON::JSONVar;
int main(){
  ArdDynamicPages pages;
  ArdJSON::Limits limits;limits.maxNodes=4096;
  for(const char* source:{SETTINGS_PAGE,SCHEDULE_PAGE,CLOCK_PAGE}) {
    V page=ArdJSON::JSON.parse(source,nullptr,limits);
    // Same descriptor normalization as ArdPortal page registration.
    for(size_t i=0;i<page["fields"].length();++i){
      V& field=page["fields"][i];
      if(!ardPortalBasicControl(field["type"].asString())){
        V resolved=field;assert(ArdHa::normalize(resolved));
        field["extended"]=true;field["persist"]=resolved["persist"];
      }
    }
    // Build the compact slice index used by the new registration pipeline.
    std::unique_ptr<ArdDynamicPages::PageStore::Entry> entry(new(std::nothrow) ArdDynamicPages::PageStore::Entry());
    entry->id=page["id"].asString();entry->source=source;
    entry->sourceLength=entry->source.length();entry->count=page["fields"].length();
    entry->condition=page.hasOwnProperty("visibleWhen")?ArdJSON::JSON.stringify(page["visibleWhen"]):String();
    entry->fields.reset(new(std::nothrow) ArdDynamicPages::PageStore::Field[entry->count]);
    size_t bytes=0;for(size_t i=0;i<entry->count;++i)bytes+=page["fields"][i]["id"].asString().length()+1;
    assert(entry->ids.reserve(bytes));size_t offset=0;
    ArdJsonFieldSlices slices(entry->source);
    assert(slices.fields(entry->count,[&](size_t i,uint16_t start,uint16_t length){
      V f=page["fields"][i];auto& meta=entry->fields[i];
      meta.offset=start;meta.length=length;meta.index=pages.count()+i;meta.idOffset=offset;
      String id=f["id"].asString();entry->ids.concat(id.c_str(),id.length()+1);offset+=id.length()+1;
      meta.extended=f["extended"].asBool();meta.persist=f["persist"].asBool();meta.transient=ArdHa::transient(f);
      meta.dependent=f.hasOwnProperty("visibleWhen")||page.hasOwnProperty("visibleWhen");
    }));
    pages.totalFields+=entry->count;pages.pages.commit(std::move(entry));
  }
  V states=V::object();
  for(const char* id:PORTAL_STATE_FIELDS){
    V field=pages.field(id);
    assert(!ArdHa::transient(field)); // applyAppState rejects transient fields before validation.
    V value;
    if(ArdHa::extended(field)){
      assert(ArdHa::normalize(field,true));value=field["default"];
    } else value=ArdDynamicPages::initial(field);
    assert(ArdDynamicPages::validValue(field,value));
    states[id]=value;
  }
  assert(states.isValid());
  for(const char* id:PORTAL_STATE_FIELDS) assert(ArdJSON::JSON.stringify(states[id]).length());
  assert(!ArdDynamicPages::validValue(pages.field("program"),V("disabled")));
  assert(ArdDynamicPages::initial(pages.field("program")).asString()=="5+2");
  V climate=ArdDynamicPages::initial(pages.field("climate"));
  for(const char* mode:{"off","heat","auto"}){
    climate["mode"]=mode;
    assert(ArdDynamicPages::validValue(pages.field("climate"),climate));
    V rule=pages.field("program")["visibleWhen"];
    assert((climate[rule["property"].asString()].asString()==rule["equals"].asString())==(String(mode)=="auto"));
  }
  climate["primary_days_active"]=true;
  assert(!ArdDynamicPages::validValue(pages.field("climate"),climate));
  for(const char* mode:{"off","heat","auto"}) {
    for(const char* program:{"disabled","5+2","6+1","7"}) {
      V page=pages.pages[1];unsigned visible=0;
      for(size_t i=1;i<page["fields"].length();++i)
        visible+=String(mode)=="auto" && page["fields"][i]["visibleWhen"]["equals"].asString()==program;
      assert(visible==(String(mode)!="auto"||String(program)=="disabled"?0u:String(program)=="7"?12u:16u));
    }
  }
  std::cout<<"Actual ArdPortal page registration and Climate mode validation passed\n";
}
