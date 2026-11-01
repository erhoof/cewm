"""Run launcher locale/default policy checks and validate translated printf arguments."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
source = (app / 'src/main.cpp').read_text()
settings = source[source.index('struct Settings {'):source.index('} g_s;') + 1] + ';'
check = r'''
#include "i18n.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
SETTINGS
PREFS
Settings NewVmDefaults() {
    Settings g_ed;
    char g_ed_name[64];
    NEW_VM_DEFAULTS
    return g_ed;
}
int main(int argc, char** argv) {
    using namespace cewm;
    assert(argc == 2);
    Settings saved = NewVmDefaults();
    assert(saved.keep_w && saved.fill && !saved.orig_res && saved.ga);
    assert(saved.fixed_w == 240);
    strcpy(saved.rom, "image.bin"); strcpy(saved.board, "devemu");
    saved.jit = false; saved.unicorn = true;
    assert(WritePref(argv[1], saved));
    Settings loaded;
    assert(ReadPref(argv[1], "/vm", loaded));
    assert(loaded.keep_w && loaded.fill && !loaded.orig_res);
    assert(loaded.unicorn && !loaded.jit && !strcmp(loaded.rom, "/vm/image.bin"));
    FILE* legacy = fopen(argv[1], "w"); assert(legacy);
    fputs("board=devemu\njit=1\nrom=image.bin\n", legacy); fclose(legacy);
    assert(ReadPref(argv[1], "/vm", loaded));
    assert(loaded.jit && !loaded.unicorn);
    legacy = fopen(argv[1], "w"); assert(legacy);
    fputs("board=asus_mypal_p525\njit=1\nunicorn=1\n", legacy); fclose(legacy);
    assert(ReadPref(argv[1], "/vm", loaded));
    assert(!loaded.jit && !loaded.unicorn);
    for (const char* locale : {"ru", "ru_RU", "ru-RU"}) assert(RussianLocale(locale));
    for (const char* locale : {"", "r", "rus", "en", "fr_FR"}) assert(!RussianLocale(locale));
    assert(!RussianLocale(nullptr));
    for (bool russian : {false, true}) {
        system_russian = russian;
        language = 0; assert(!strcmp(L("ru", "en"), russian ? "ru" : "en"));
        language = 1; assert(!strcmp(L("ru", "en"), "en"));
        language = 2; assert(!strcmp(L("ru", "en"), "ru"));
    }
    language = 1;
    assert(RomLabel("Pocket PC 2000, сборка 9456, испанский") == "Pocket PC 2000, build 9456, Spanish");
    assert(RomLabel("Windows CE (русский)") == "Windows CE (Russian)");
    language = 2;
    assert(RomLabel("Windows CE (русский)") == "Windows CE (русский)");
    assert(DeviceEmulator("devemu")); assert(DeviceEmulator("devemu_wvga"));
    assert(!DeviceEmulator("asus_mypal_p525")); assert(!DeviceEmulator("jornada_720"));
#ifdef CEWM_HAVE_UNICORN
    assert(Settings().unicorn && !Settings().jit);
#elif defined(CEWM_HAVE_DYNARMIC)
    assert(Settings().jit && !Settings().unicorn);
#else
    assert(!Settings().jit && !Settings().unicorn);
#endif
}
'''.replace('SETTINGS', settings).replace('PREFS', source[source.index('bool SetKey('):source.index('enum class Mode')])
check = check.replace('NEW_VM_DEFAULTS', source[source.index('        g_ed = Settings();'):source.index('        g_ed_orig.clear();')])
with tempfile.TemporaryDirectory(prefix='cewm-launcher-test-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    cpp.write_text(check)
    for defines in [[], ['-DCEWM_HAVE_UNICORN'], ['-DCEWM_HAVE_DYNARMIC'], ['-DCEWM_HAVE_UNICORN', '-DCEWM_HAVE_DYNARMIC']]:
        binary = str(Path(tmp) / 'check')
        subprocess.run(['c++', '-std=c++20', '-include', 'initializer_list', '-I' + str(app / 'src'), *defines, str(cpp), '-o', binary], check=True)
        subprocess.run([binary, str(Path(tmp) / 'vm.pref')], check=True)
q = r'"(?:[^"\\]|\\.)*"'
count = 0
for file in ['src/main.cpp', 'host/rom_download.cpp', 'host/p525_prepare.cpp']:
    for ru, en in re.findall(r'cewm::L\((' + q + r'), (' + q + r')\)', (app / file).read_text()):
        ru, en = json.loads(ru), json.loads(en)
        formats = lambda text: re.findall(r'%(?:[-+0 #]*\d*(?:\.\d+)?[hlz]*[diuoxXfFeEgGcs%])', text)
        assert formats(ru) == formats(en), (ru, en)
        count += 1
assert count > 100
print(f'PASS: locale selection, JIT build defaults, board policy, {count} translation formats')
