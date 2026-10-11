"""Exercise installed ArdPortal command/save methods with a busy-storage mock."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
lib=root.parent/'libraries/ArdPortal/src'
source=(lib/'ArdAppControlsImpl.h').read_text()
def method(name):
    start=source.index(name)
    opening=source.index('{',start)
    depth=1; end=opening+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end+=1
    return source[start:end]
cpp=r'''
#include "portal_features.h"
#define ARDPORTAL_DEFINE_HA_DATA
#include "Arduino.h"
struct SchemaHeapMock {uint32_t getFreeHeap(){return 1000000;} uint32_t getMaxFreeBlockSize(){return 1000000;}} ESP;
#include "DynamicPages.h"
#include <cassert>
using V=ArdJSON::JSONVar;
enum class ChangeSource {Mqtt,Application};
uint32_t fakeNow=0; std::function<void(uint32_t)> onAdvance;
void advanceTime(uint32_t delta){fakeNow+=delta;}
ArdJSON::Limits snapshotLimits(){return ArdJSON::Limits();}
struct Fields {V descriptor=V::object(); const V& field(const String&){return descriptor;} size_t indexOf(const String& key){return key=="mode"?0:1;} size_t count(){return 1;} String idAt(size_t){return "mode";}};
struct Portal {
 V _appConfig=V::object(),_config=V::object(),pending;
 ArdJSON::JSONClass& _json=ArdJSON::JSON;
 struct {void dependencyChanged(const String&) {}} _homeAssistant;
 Fields _dynamic; bool busy=true,_httpWaitingStorage=false,allowSave=true;
 unsigned _appRevision=0; ChangeSource _saveSource=ChangeSource::Mqtt;
 struct {uint32_t appStateIntervalMs=0;} _options;
 std::function<void(const String&,const V&,ChangeSource)> _appChanged;
 std::function<V()> read;
 V getAppConfigValue(const char*){return read();}
 bool portalAndAppConfigBusy(){return busy;}
 bool scheduleAppPatch(const V& app,ChangeSource){if(!allowSave)return false;pending=_appConfig;V keys=app.keys();for(size_t i=0;i<keys.length();++i){String key=keys[i].asString();pending[key]=app[key];}return true;}
 bool scheduleConfig(const V&,const V& app,ChangeSource,bool immediate){assert(!immediate);if(!allowSave)return false;pending=app;return true;}
};
class ArdAppControls {
public:
 Portal& _portal; V _appState=V::object(),_mqttAppQueue=V::object();
 ArdAppConfigFieldMask _mqttAckPending=0,_mqttAckInFlight=0,_stateDirty=0;uint32_t _stateSince=0;
 ArdAppControls(Portal&p):_portal(p){p.read=[this](){return _appState.hasOwnProperty("mode")?V(_appState["mode"]):V(_portal._appConfig["mode"]);};}
 bool applyAppState(const String& key,const V& value,ChangeSource source){_appState[key]=value;if(_portal._appChanged)_portal._appChanged(key,value,source);return true;}
 void markDirty(const String&){_stateDirty|=1;}
 ArdAppConfigFieldMask stateMask(){return 1;}
 bool acceptMqttState(const String&,V);
 void prepareMqtt(uint32_t);
 void applyAppConfig(V);
};
'''
# JSON class name is inferred from the real API to avoid duplicating its definition.
cpp=cpp.replace('ArdJSON::JSONClass&','decltype(ArdJSON::JSON)&')
cpp+='\n'.join(method(n) for n in ['bool ArdAppControls::acceptMqttState','void ArdAppControls::prepareMqtt','void ArdAppControls::applyAppConfig'])
cpp+=r'''
int main(){
 Portal p;ArdAppControls c(p);unsigned calls=0;
 p._appChanged=[&](const String&,const V&,ChangeSource){++calls;};
 p._appConfig["mode"]="off";
 assert(c.acceptMqttState("mode",V("heat")));assert(calls==1);
 c.prepareMqtt(0);assert(calls==1&&c._mqttAppQueue.length()==1);
 assert(c.acceptMqttState("mode",V("auto")));assert(calls==2);
 p.busy=false;p.allowSave=false;c.prepareMqtt(0);assert(c._mqttAppQueue.length()==1);
 p.allowSave=true;c.prepareMqtt(0);assert(calls==2&&c._mqttAppQueue.length()==0);
 assert(p.pending["mode"].asString()=="auto");
 c._appState["mode"]="heat"; // MCU correction before commit must survive.
 c.applyAppConfig(p.pending);assert(calls==2);assert(p.getAppConfigValue("mode").asString()=="heat");
 c._appState=V::object();V next=V::object();next["mode"]="off";
 c.applyAppConfig(next);assert(calls==2); // no command replay from committed settings
 p._saveSource=ChangeSource::Application;next["mode"]="heat";
 c.applyAppConfig(next);assert(calls==3); // unrelated application updates still notify
}
'''
with tempfile.TemporaryDirectory(prefix='mqtt-command-test-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(cpp)
    binary=Path(tmp)/'test'
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I',str(root/'tests/host'),'-I',str(root),'-I',str(lib),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('ArdPortal MQTT command/save regression passed')
