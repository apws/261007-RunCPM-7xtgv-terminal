"""Exercise production RAM-cache/storage/guest-access code without a device."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IniStorageTests(unittest.TestCase):
    def test_transactions_cache_and_guest_edits(self):
        persistence = (ROOT / 'src/terminal/terminal_persistence.h').read_text()
        helpers = '//' + persistence.split('// Accessed only by the RunCPM/storage owner.', 1)[1].split(
            'static bool writeTerminalConfigErrorReport', 1)[0]
        poll = 'bool terminalConfigStoragePoll() {' + persistence.split(
            'bool terminalConfigStoragePoll() {', 1)[1]
        source = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "src/terminal/ini_text.h"
#include "src/terminal/display_policy.h"
#include "src/terminal/terminal_runtime.h"
const char *TerminalConfigPath="A/0/7XTGVTRM.INI";
const char *TerminalConfigNewPath="A/0/7XTCFG.NEW";
const char *TerminalConfigBackupPath="A/0/7XTCFG.BAK";
constexpr int FILE_READ=1, O_CREAT=2, O_WRITE=4;
std::map<std::string,std::string> files;
std::vector<std::string> readOpens;
bool failWrite=false, badVerify=false, shortRead=false, failOpen=false, failRename=false;
struct File {
    std::string path;
    size_t pos=0;
    explicit operator bool() const { return !path.empty(); }
    size_t size() const { return files.at(path).size(); }
    int read(uint8_t *p,size_t n) {
        size_t count=std::min(n,size()-pos);
        if (shortRead && path==TerminalConfigPath && count) --count;
        memcpy(p,files[path].data()+pos,count); pos+=count;
        if (badVerify && path==TerminalConfigNewPath && count) p[0]^=1;
        return int(count);
    }
    int read() { return pos<size() ? uint8_t(files[path][pos++]) : -1; }
    size_t write(const uint8_t *p,size_t n) {
        if (failWrite) return 0;
        files[path].append(reinterpret_cast<const char *>(p),n); return n;
    }
    size_t write(uint8_t b) { return write(&b,1); }
    void flush() {}
    void close() { path.clear(); }
};
struct Storage {
    bool exists(const char *p) { return files.count(p); }
    bool remove(const char *p) { return files.erase(p); }
    bool rename(const char *from,const char *to) {
        if ((failRename && std::string(from)==TerminalConfigNewPath) ||
            !exists(from) || exists(to)) return false;
        files[to]=files[from]; files.erase(from); return true;
    }
    File open(const char *p,int mode) {
        if (mode==FILE_READ) {
            readOpens.emplace_back(p);
            if (failOpen || !exists(p)) return {};
        } else if (!exists(p)) files[p]="";
        return {p,0};
    }
} SD;
bool pending=false, paused=false;
uint8_t level=17;
void terminalPause(bool value) { paused=value; }
void terminalDiskActivity(bool) {}
void terminalConfigForceSave() {}
bool terminalConfigSaveDue(uint8_t &l,uint8_t &z,uint8_t *p,uint8_t *dirty) {
    l=level; z=2; *p=3; *dirty=ConfigBacklightDirty | ConfigPaletteDirty | ConfigZoomDirty;
    return pending;
}
void terminalConfigSaveComplete(bool saved) { if (saved) pending=false; }
bool terminalConfigStoragePoll();
''' + helpers + poll + r'''
int main() {
    const std::string original="[global]\r\nbacklight_level=1\r\n;keep\r\n";
    files[TerminalConfigPath]=original+char(0x1a)+"record padding";
    assert(loadTerminalConfigText());
    assert(terminalConfigText.size()==original.size());
    pending=true;
    assert(terminalConfigStoragePoll()); assert(!pending && !paused);
    assert(files[TerminalConfigPath].find("backlight_level=17")!=std::string::npos);
    level=18; pending=true;
    assert(terminalConfigStoragePoll());
    // Only boot read opened the original INI; later saves read .NEW to verify.
    assert(std::count(readOpens.begin(),readOpens.end(),TerminalConfigPath)==1);
    const std::string saved=files[TerminalConfigPath];
    for (int failure=0;failure<3;++failure) {
        failWrite=failure==0; badVerify=failure==1; failRename=failure==2;
        level=19; pending=true;
        assert(!terminalConfigStoragePoll()); assert(pending && !paused);
        assert(files[TerminalConfigPath]==saved); // including rename rollback
    }
    failWrite=badVerify=failRename=false;
    assert(terminalConfigStoragePoll());
    // A download sees even pending settings, not the old lazy-save file.
    level=20; pending=true;
    assert(terminalConfigFileAccess("./a/0/7xtgvtrm.ini",false));
    assert(!pending && files[TerminalConfigPath].find("backlight_level=20")!=std::string::npos);
    // Another DU with the same name does not invalidate the cache.
    assert(terminalConfigFileAccess("A/7/7XTGVTRM.INI",true));
    assert(terminalConfigText.valid() && !terminalConfigGuestWriting);
    // Upload/edit: flush before first mutation, defer ALL saves until close.
    level=21; pending=true;
    assert(terminalConfigFileAccess(TerminalConfigPath,true));
    assert(!pending && !terminalConfigText.valid() && terminalConfigGuestWriting);
    files[TerminalConfigPath]="partial";
    pending=true;
    assert(!terminalConfigStoragePoll()); assert(files[TerminalConfigPath]=="partial");
    const std::string uploaded="[global]\n;uploaded comment\ncrt_rampup_ms=888\n";
    files[TerminalConfigPath]=uploaded+char(0x1a)+"padding";
    terminalConfigFileClosed(TerminalConfigPath);
    assert(terminalConfigText.valid() && !terminalConfigGuestWriting);
    assert(terminalConfigStoragePoll());
    assert(files[TerminalConfigPath].find(uploaded)!=std::string::npos);
    // Aborted writer / ERA / rename-style editor: CCP is the fallback boundary.
    assert(terminalConfigFileAccess(TerminalConfigPath,true));
    files.erase(TerminalConfigPath);
    terminalConfigGuestIdle();
    assert(terminalConfigText.valid() && terminalConfigText.size()==0);
    pending=true; assert(terminalConfigStoragePoll());
    // Failed reads/open/oversize refuse to replace a disk file with partial RAM.
    for (int failure=0;failure<3;++failure) {
        terminalConfigText.invalidate();
        shortRead=failure==0; failOpen=failure==1;
        files[TerminalConfigPath]=failure==2 ? std::string(65537,'x') : original;
        const std::string before=files[TerminalConfigPath];
        pending=true; assert(!terminalConfigStoragePoll());
        assert(!terminalConfigText.valid() && files[TerminalConfigPath]==before);
        assert(std::string(terminalConfigCacheFailure)==
               (failure==0 ? "file read incomplete" :
                failure==1 ? "file unavailable" : "file exceeds 64 KiB limit"));
        shortRead=failOpen=false;
    }
    // Sync failure prevents a guest writer from starting at all.
    files[TerminalConfigPath]=original;
    assert(loadTerminalConfigText()); pending=true; failWrite=true;
    assert(!terminalConfigFileAccess(TerminalConfigPath,true));
    assert(!terminalConfigGuestWriting && files[TerminalConfigPath]==original);
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-ini-storage-') as temp:
            path = Path(temp) / 'test.cpp'
            path.write_text(source)
            binary = Path(temp) / 'test'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_guest_hooks_are_wired(self):
        abstraction = (ROOT / 'abstraction_arduino_7xtgv.h').read_text()
        for name in ('_sys_fopen_w', '_sys_openfile', '_sys_makefile', '_sys_deletefile',
                     '_sys_renamefile', '_sys_extendfile', '_Truncate'):
            function = abstraction.split(name + '(', 1)[1].split('\n}', 1)[0]
            self.assertIn('_xtgvConfigAccess(', function)
        self.assertIn('XTGV_MODIFIED_FILE_CLOSED((const char *)filename);',
                      (ROOT / 'disk.h').read_text())
        self.assertIn('XTGV_CONFIG_GUEST_IDLE();', (ROOT / 'ccp7xf.h').read_text())


if __name__ == '__main__':
    unittest.main()
