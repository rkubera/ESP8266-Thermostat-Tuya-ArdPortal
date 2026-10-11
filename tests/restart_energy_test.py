"""Execute the actual restart callback and energy save code with a storage mock."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'portal.ino').read_text()
start=source.index('  portal.onBeforeRestart(')
end=source.index('  portal.onAppConfigValueChanged',start)
callback=source[start:end]
energy=(root/'energy.ino').read_text()
store=energy[energy.index('bool storeEnergy()'):energy.index('void updateEnergy()')]
cpp=r'''
#include "energy_meter.h"
#include <functional>
#include <cassert>
#include <cstring>
struct ArdPortal {
 enum class RestartReason {Portal,FirmwareUpdate,StorageFormat};
 std::function<void(RestartReason)> before;
 int writes=0;double saved=-1;bool accepted=true,busy=false,ready=true;
 void onBeforeRestart(std::function<void(RestartReason)> cb){before=cb;}
 bool portalAndAppConfigReady(){return ready;}
 bool portalAndAppConfigBusy(){return busy;}
 bool setAppConfigValue(const char* key,double value){assert(std::strcmp(key,"energy_total")==0);++writes;saved=value;return accepted;}
} portal;
EnergyMeter energyMeter;
bool energySavePending=false;double energySaveValue=0;
uint32_t now=1000;
void updateEnergy(){energyMeter.update(now,true,true,1000);}
int errors=0;
void diagnostic(const char*){++errors;}
'''+store+r'''
int main(){
'''+callback+r'''
 energyMeter.restore(1.23456789);
 energyMeter.update(0,true,true,1000);
 portal.before(ArdPortal::RestartReason::Portal);
 double expected=1.23456789+1000.0/3600000.0;
 assert(std::fabs(portal.saved-expected)<1e-12);
 assert(energySavePending && energySaveValue==portal.saved);
 now=2000;
 portal.before(ArdPortal::RestartReason::FirmwareUpdate);
 assert(std::fabs(portal.saved-(expected+1000.0/3600000.0))<1e-12);
 int writes=portal.writes;double total=energyMeter.total();now=3000;
 portal.before(ArdPortal::RestartReason::StorageFormat);
 assert(portal.writes==writes && energyMeter.total()==total);
 portal.accepted=false;energySavePending=false;
 portal.before(ArdPortal::RestartReason::Portal);
 assert(errors==1 && !energySavePending);
}
'''
with tempfile.TemporaryDirectory(prefix='restart-energy-test-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(cpp)
    binary=Path(tmp)/'test'
    subprocess.run(['clang++','-std=c++11','-fsanitize=address,undefined','-I',str(root),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Before-restart energy save regression passed')
