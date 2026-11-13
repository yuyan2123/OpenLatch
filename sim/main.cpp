#include <iostream>
#include <sstream>
#include <string>
#include "VirtualHardware.h"

void status(const Rig& r) {
  std::cout << "t=" << r.relay.now << "ms wifi=" << r.wifi
            << " verified_session=" << r.session
            << " GPIO4=" << (r.relay.high ? "HIGH" : "LOW")
            << " virtual_COM_NO=" << (r.relay.high ? "CLOSED" : "OPEN")
            << " action=" << r.controller.actionOn()
            << " fault=" << r.controller.faulted()
            << " door_state=UNKNOWN\n";
}
void result(door::Result r) { std::cout << "command_result=" << door::name(r) << '\n'; }
int main(int argc, char** argv) {
  Rig r;
  std::cout << "SIMULATION ONLY: no ESP32, network listener or HomeKit authentication.\n";
  if (argc == 1 || std::string(argv[1]) == "--demo") {
    status(r);
    r.ready();
    result(r.on()); status(r);
    r.advance(100); result(r.on());
    r.advance(300); status(r);
    result(r.on());
    r.advance(2000); result(r.controller.infrared(r.now()));
    result(r.on());
    r.advance(100); r.network(false, false); status(r);
    r.advance(3000); r.network(true, true); status(r);
    result(r.on(false));
    r.reboot(); status(r);
    return 0;
  }
  if (std::string(argv[1]) != "--interactive") return 2;
  std::cout << "Commands: wifi on|off, session on|off, on, off, unauth, ir,\n"
               "advance MS, stall MS, reboot, fault, status, trace, quit\n"
               "Time is virtual; use advance to run it. session simulates verified HAP.\n";
  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command, value;
    input >> command;
    if (command == "quit") break;
    if (command == "on") result(r.on());
    else if (command == "off") result(r.controller.homekit(r.now(), r.session, false));
    else if (command == "unauth") result(r.on(false));
    else if (command == "ir") result(r.controller.infrared(r.now()));
    else if (command == "wifi" || command == "session") {
      input >> value;
      if (value != "on" && value != "off") { std::cout << "Expected on/off\n"; continue; }
      if (command == "wifi") r.network(value == "on", false);
      else r.network(r.wifi, r.wifi && value == "on");
    } else if (command == "advance" || command == "stall") {
      int64_t ms = -1;
      if (!(input >> ms) || ms < 0 || ms > 86400000) {
        std::cout << "Expected 0..86400000 milliseconds\n"; continue;
      }
      r.advance(uint64_t(ms), command == "advance");
    } else if (command == "reboot") r.reboot();
    else if (command == "fault") r.controller.trip(r.now());
    else if (command == "trace") {
      for (auto event : r.relay.trace)
        std::cout << event.at << "ms GPIO4=" << (event.on ? "HIGH" : "LOW") << '\n';
    } else if (command != "status") { std::cout << "Unknown command\n"; continue; }
    status(r);
  }
}
