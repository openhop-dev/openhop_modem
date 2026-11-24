"""Adapt legacy recording Wi-Fi fixtures to the real fd/send SDK boundary.

Only scratch copies are rewritten. Keep the original Wi-Fi event/value fake
and its assertions; share the finite-capacity socket port with the TCP tests.
Every replacement must match exactly once, so fixture ABI drift fails loudly.
"""
from pathlib import Path
import shutil

FW = Path(__file__).resolve().parents[2]
PORT = FW / 'tools/radio_runtime_stubs/lwip/socket_port.h'


def socket_source(source):
    changes = [
        ('#include <functional>', '#include <functional>\n#include "' + str(PORT) + '"'),
        ('struct SocketStub {bool open=true;', 'struct SocketStub : RadioSocketPort {'),
        (' void stop(){if(s)s->open=false;}', ' void stop(){closeRadioSocket(s);}\n'
         ' int fd() const {if(!s || !s->open)return -1;'
         'if(s->modelFd<0)registerRadioSocket(s);return s->nativeFd>=0?s->nativeFd:s->modelFd;}'),
        (' size_t write(const uint8_t*,size_t n){return n;}',
         ' size_t write(const uint8_t* p,size_t n){if(!s || !s->open)return 0;'
         '++s->legacyWriteCalls;auto count=radioSocketSend(fd(),p,n,MSG_DONTWAIT);'
         'return count>0?size_t(count):0;}'),
    ]
    for old, new in changes:
        if source.count(old) != 1:
            raise ValueError('Wi-Fi socket port fixture drift: ' + old)
        source = source.replace(old, new)
    return source


def stage_socket_port(directory, copy_wifi_stubs=True):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    if copy_wifi_stubs:
        shutil.copytree(FW / 'tools/wifi_recovery_stubs', directory, dirs_exist_ok=True)
    original = (FW / 'tools/wifi_recovery_stubs/WiFi.h').read_text()
    (directory / 'WiFi.h').write_text(socket_source(original))
    lwip = directory / 'lwip'
    lwip.mkdir(exist_ok=True)
    (lwip / 'sockets.h').write_text('#pragma once\n#include "' + str(PORT) + '"\n#define send radioSocketSend\n')
