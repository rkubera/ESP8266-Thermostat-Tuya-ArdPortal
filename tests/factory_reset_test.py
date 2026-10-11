"""Execute the sketch factory reset hook without resetting physical hardware."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'portal.ino').read_text()
a=s.index('  portal.onBeforeFactoryReset(')
b=s.index('  portal.onPortalConfigChanged',a)
cpp=r'''#include <functional>
#include <cassert>
struct Portal {
 std::function<void()> before;
 void onBeforeFactoryReset(std::function<void()> cb){before=cb;}
} portal;
bool accepted=false;
unsigned requests=0,logs=0;
bool requestMcuFactoryReset(){++requests;return accepted;}
void diagnostic(const char*){++logs;}
int main(){
'''+s[a:b]+r'''
 portal.before();assert(requests==1 && logs==1);
 accepted=true;portal.before();assert(requests==2 && logs==1);
}
'''
with tempfile.TemporaryDirectory(prefix='factory-reset-test-') as tmp:
 path=Path(tmp)/'test.cpp';path.write_text(cpp);binary=Path(tmp)/'test'
 subprocess.run(['clang++','-std=c++11','-fsanitize=address,undefined',str(path),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('Factory reset hook regression passed')
