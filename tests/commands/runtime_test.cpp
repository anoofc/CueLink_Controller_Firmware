#include "commands.h"
#include "fixtures.h"
#include "pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
SerialStub Serial;
static bool levels[8];
static unsigned opens, sends, stops;
static bool openOk = true, sendOk = true;
static uint16_t failPort;
static Command::Packet submitted;
static std::vector<char> order;
static std::vector<uint16_t> ports;
int digitalRead(uint8_t pin) { return levels[pin] ? LOW : HIGH; }
void pinMode(uint8_t, int mode) { assert(mode == INPUT_PULLUP); }
void Network::begin(uint32_t) { state_ = State::Reconnecting; }
void Network::update(uint32_t signal) {
  if (!signal) { if (connected()) ++losses_; state_ = State::Reconnecting; ip_ = IPAddress(); }
  else { state_ = State::Connected; ip_ = IPAddress(signal); }
}
bool UdpTransport::open(const uint16_t*, size_t) {
  if (active()) return true;
  ++opens;
  if (!openOk) return false;
  socket_ = 1; localPort_ = 50000; return true;
}
bool UdpTransport::send(const Command::IPv4&, uint16_t port, const Command::Packet& packet) {
  ++sends; submitted = packet; ports.push_back(port);
  order.push_back(packet.bytes[0] == '/' ? 'O' : 'U');
  return sendOk && port != failPort;
}
void UdpTransport::stop() { if (active()) ++stops; socket_ = -1; localPort_ = 0; }
struct Port : Midi::Port {
  bool online = true, ok = true; uint32_t epoch = 0;
  std::vector<Midi::Packet> packets;
  bool mounted() const override { return online; }
  bool ready() const override { return online; }
  uint32_t sessionEpoch() const override { return epoch; }
  bool send(const Midi::Packet& p) override { packets.push_back(p); order.push_back('M'); return ok; }
};
struct Fixture {
  Command::Destination dest[9] = {{"192.0.2.42",7000},{"192.0.2.43",9000}};
  Triggers::Osc osc{TestCommands::buttons[0].action.oscAddress, TestCommands::connect, 1, {dest,1}};
  Triggers::Udp udp{TestCommands::raw3,10,{dest+1,1}};
  Midi::Message note = Midi::Message::pulse(1,60,100);
  Triggers::Trigger slots[5] = {Triggers::Trigger::osc(&osc),Triggers::Trigger::udp(&udp),Triggers::Trigger::midi(&note),Triggers::Trigger::udp(&udp),Triggers::Trigger::osc(&osc)};
  Triggers::Button mappings[4] = {{"B1",slots,3},{"B2",slots,1},{"B3",slots+1,1},{"B4",slots+1,1}};
  Triggers::Configuration configuration{mappings};
  Port port; Midi::Engine midi{port}; Commands commands{configuration,midi};
  ButtonController buttons; Network network; uint32_t now = 0;
  Fixture(bool online = true) {
    opens=sends=stops=0; openOk=sendOk=true; failPort=0; order.clear(); ports.clear();
    memset(levels,0,sizeof(levels)); Serial.output.clear();
    commands.begin(); buttons.begin(now); if(online) network.update(1); tick(30);
    assert(sends==0 && port.packets.empty());
  }
  void tick(uint32_t elapsed, bool reconnect = false) {
    now+=elapsed; bool wifi=network.connected(), usb=port.ready(); auto ev=buttons.update(now);
    if(reconnect) network.update(1);
    commands.update(network,now); commands.handleEvents(buttons.mode(),ev,network,wifi,usb,now);
  }
  void edge(uint8_t b,bool down) { levels[Pins::buttons[b]]=down; tick(1); tick(30); }
  void event(uint8_t mask=1,bool wifi=true,bool usb=true) {
    ButtonController::Events ev; ev.shortPressMask=mask;
    commands.handleEvents(Mode::Run,ev,network,wifi,usb,++now);
  }
};
int main() {
  for(size_t count=0;count<=5;++count) {
    Fixture f; f.mappings[0].count=count; f.commands.begin(); f.event();
    assert(f.commands.buttonValid(0)==(count<=4));
    if(count<=4) { assert(f.commands.summary().attempted==count && f.commands.summary().sent==count); }
    else assert(sends==0 && f.port.packets.empty() && f.commands.summary().invalid);
  }
  {
    Fixture f; f.mappings[0].count=4; f.commands.begin(); f.event();
    assert(std::string(order.begin(),order.end())=="OUMU");
    assert(submitted.length==10 && !memcmp(submitted.bytes,"CUELINK_B3",10));
    assert(f.commands.summary().sent==4);
    f.slots[1]=Triggers::Trigger::udp(&f.udp,false); f.commands.begin(); order.clear(); f.event();
    assert(std::string(order.begin(),order.end())=="OMU");
  }
  {
    Fixture f(false); f.event(1,false,true);
    assert(sends==0 && f.port.packets.size()==1 && f.commands.summary().partial);
    f.network.update(1); f.tick(1000); assert(sends==0); // no replay
    f.port.online=false; f.event(); assert(sends==2 && f.commands.summary().sent==2);
    f.port.online=true; f.tick(1000); assert(sends==2 && f.port.packets.size()==2); // only prior scheduled release
  }
  {
    Fixture f; f.configuration.useMaster=true; f.configuration.master={f.dest,2}; f.commands.begin(); f.event();
    assert(sends==4 && f.port.packets.size()==1 && ports[0]==7000 && ports[1]==9000);
    assert(f.osc.destinations.count==1 && f.udp.destinations.items==f.dest+1);
    f.configuration.master={nullptr,0}; f.commands.begin(); f.event();
    assert(sends==4 && f.port.packets.size()==2 && f.commands.summary().sent==1);
  }
  {
    Fixture f; for(auto& d:f.dest) d={"192.0.2.42",7000};
    f.osc.destinations={f.dest,8}; f.commands.begin(); f.event(2); assert(sends==8);
    f.osc.destinations.count=9; f.commands.begin(); f.event();
    assert(sends==9 && f.port.packets.size()==1 && f.commands.summary().sent==2);
    f.osc.destinations={f.dest,2}; f.dest[0].ipv4=""; f.commands.begin(); f.event(2);
    assert(sends==10 && f.commands.report(1,0).result==Commands::Result::Partial);
    assert(f.commands.report(1,0).destinationSuccess==1 && f.commands.summary().sent==0);
  }
  {
    Fixture f; f.osc.destinations={f.dest,2}; failPort=7000; f.commands.begin(); f.event();
    assert(sends==3 && f.port.packets.size()==1 && f.commands.summary().sent==2);
    assert(f.commands.report(0,0).destinations[0]==Commands::Result::LocalFailure);
    assert(f.commands.report(0,0).destinations[1]==Commands::Result::Submitted);
    assert(f.commands.summary().partial);
    f.tick(10000); assert(sends==3); // failed destinations never retried
  }
  {
    Fixture f; f.mappings[0].count=4; f.port.ok=false; f.commands.begin(); f.event();
    assert(sends==3 && f.commands.summary().sent==3 && f.commands.summary().partial);
    assert(f.commands.report(0,2).result==Commands::Result::LocalFailure);
    f.configuration.useMaster=true; f.configuration.master={f.dest,9}; f.commands.begin(); f.event();
    assert(sends==3 && f.commands.summary().sent==0); // reject oversized master, MIDI independently attempted
    f.configuration.master={f.dest,2}; f.dest[0].ipv4=""; f.port.ok=true; f.commands.begin(); f.event();
    assert(sends==6 && f.commands.summary().sent==1 && f.commands.summary().partial);
  }
  {
    Fixture f; f.note.channel=17; f.udp.length=1025; f.commands.begin(); f.event();
    assert(sends==1 && f.port.packets.empty() && f.commands.summary().sent==1);
    ButtonController::Events ev; ev.shortPressMask=15; ev.modeChanged=true;
    f.commands.handleEvents(Mode::Run,ev,f.network,true,true,1);
    ev.modeChanged=false; f.commands.handleEvents(Mode::Diagnostics,ev,f.network,true,true,1); assert(sends==1);
  }
  {
    Fixture f(false); levels[Pins::buttons[1]]=true; f.tick(1); f.tick(30,true);
    assert(sends==0); f.tick(1000); assert(sends==0);
  }
  {
    Fixture f; f.edge(1,true); f.tick(6000); f.edge(1,false); assert(sends==1);
    f.edge(3,true); assert(sends==1); f.edge(3,false); assert(sends==2);
    f.edge(3,true); f.tick(5000); assert(f.buttons.mode()==Mode::Diagnostics && sends==2);
    f.edge(3,false);
    // Existing pulse must release even in diagnostic mode.
    assert(f.midi.submit(f.note,true,f.now)==Midi::Result::Submitted); f.tick(100);
    assert(f.midi.pendingCount()==0 && f.port.packets.back().bytes[1]==0x80);
    for(uint8_t i=0;i<ButtonController::pageCount;++i) { f.edge(1,true); f.edge(1,false); }
    assert(sends==2); f.edge(3,true); f.tick(5000); f.edge(3,false);
    assert(f.buttons.mode()==Mode::Run && sends==2);
  }
  {
    Fixture f; f.now=UINT32_MAX-500; f.event(); f.tick(1499); assert(f.commands.feedbackActive());
    f.tick(1); assert(!f.commands.feedbackActive());
    f.network.update(0); f.tick(1); assert(stops==1);
    f.network.update(2); f.tick(1); assert(sends==2);
  }
  puts("Trigger counts/order, routing/fanout bounds, isolated failures, offline drops, button gating and rollover passed.");
}
