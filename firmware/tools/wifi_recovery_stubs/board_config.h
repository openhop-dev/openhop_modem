#pragma once
struct BoardStub { struct {bool enabled=false; int gpio3_pin=-1,gpio14_pin=-1;} wifi_antenna_switch;
 const char* mdns_prefix="test"; int pin_user_button=-1; bool user_button_active_low=true;
};
inline BoardStub BOARD;
