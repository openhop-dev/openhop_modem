// =============================================================
// node_state.cpp — see header.
// =============================================================
#if defined(BOARD_HELTEC_T114)

#include "node_state.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

namespace NodeState {

static const char* PATH_NAME    = "/state_name";
static const char* PATH_STANDBY = "/state_stby";

static char  s_name[24]  = "";
static bool  s_standby   = false;
static bool  s_ready     = false;

static void readFile(const char* path, char* buf, size_t cap) {
    File f(InternalFS);
    if (!f.open(path, FILE_O_READ)) { buf[0] = '\0'; return; }
    int n = f.read((uint8_t*)buf, cap - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    f.close();
}

static void writeFile(const char* path, const void* data, size_t len) {
    InternalFS.remove(path);   // overwrite
    File f(InternalFS);
    if (!f.open(path, FILE_O_WRITE)) return;
    f.write((const uint8_t*)data, len);
    f.close();
}

void begin() {
    if (s_ready) return;
    if (!InternalFS.begin()) {
        // First time the LittleFS partition was used — format it.
        InternalFS.format();
        InternalFS.begin();
    }

    readFile(PATH_NAME, s_name, sizeof(s_name));

    char tmp[4] = {};
    readFile(PATH_STANDBY, tmp, sizeof(tmp));
    s_standby = (tmp[0] == '1');

    s_ready = true;
}

const char* getDisplayName() { return s_name; }
bool getStandby()            { return s_standby; }

void setDisplayName(const char* name) {
    if (!name) name = "";
    if (strncmp(s_name, name, sizeof(s_name)) == 0) return;   // unchanged
    snprintf(s_name, sizeof(s_name), "%s", name);
    writeFile(PATH_NAME, s_name, strlen(s_name));
}

void setStandby(bool on) {
    if (s_standby == on) return;
    s_standby = on;
    char c = on ? '1' : '0';
    writeFile(PATH_STANDBY, &c, 1);
}

}   // namespace NodeState

#endif   // BOARD_HELTEC_T114
