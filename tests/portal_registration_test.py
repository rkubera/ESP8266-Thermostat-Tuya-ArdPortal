"""Exercise the sketch's asynchronous registration flow with delayed completion."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'portal.ino').read_text()
service=s[s.index('void servicePortalRegistration()'):s.index('void setupPortal()')]
callback=s[s.index('  portal.onAppConfigPageRegistrationFinished'):s.index('  portal.onBeforeRestart')]
cpp=r'''
#include "Arduino.h"
#include <cassert>
struct Portal {
 bool busy=false,ready=false,accept=true;unsigned starts=0;
 std::function<void(bool)> finished;
 struct {String reason="error";} error;
 bool appConfigPageRegistrationBusy(){return busy;}
 bool portalAndAppConfigReady(){return ready;}
 bool startAppConfigPageRegistration(const char*){++starts;if(!accept)return false;busy=true;return true;}
 const decltype(error)& appConfigRegistrationError(){return error;}
 void onAppConfigPageRegistrationFinished(std::function<void(bool)> cb){finished=cb;}
 void complete(bool success){busy=false;finished(success);}
} portal;
unsigned portalRegistrationIndex=0,bridgeStateRevision=0;
bool portalRegistrationFailed=false,portalValuesReady=false,forcePortalStateRefresh=false;
uint8_t portalPageMask=0;unsigned seeds=0,errors=0;
const char* PORTAL_PAGE_DEFINITIONS[]={"settings","schedule","clock"};
const uint8_t PORTAL_PAGE_BITS[]={2,4,8};
void seedPortalState(){++seeds;}
void configureTimezone(){}
void diagnostic(const String&){++errors;}
'''+service+'\nint main(){\n'+callback+r'''
 for(unsigned i=0;i<3;++i){
   servicePortalRegistration();assert(portal.busy && portal.starts==i+1 && !portalValuesReady);
   servicePortalRegistration();assert(portal.starts==i+1 && seeds==0);
   portal.complete(true);assert(portalRegistrationIndex==i+1);
 }
 assert(portalPageMask==14 && seeds==0);
 servicePortalRegistration();assert(seeds==0);
 portal.ready=true;servicePortalRegistration();assert(seeds==1 && portalValuesReady && forcePortalStateRefresh);
 servicePortalRegistration();assert(seeds==1);
 portalRegistrationIndex=0;portalValuesReady=false;portalPageMask=0;
 servicePortalRegistration();portal.complete(false);assert(portalRegistrationFailed && portalPageMask==0);
 unsigned starts=portal.starts;servicePortalRegistration();assert(portal.starts==starts);
 portalRegistrationFailed=false;portal.accept=false;
 servicePortalRegistration();assert(portalRegistrationFailed && errors==2);
}
'''
with tempfile.TemporaryDirectory(prefix='portal-registration-test-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(cpp);binary=Path(tmp)/'test'
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I',str(root/'tests/host'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Asynchronous portal registration regression passed')
