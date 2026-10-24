#include <Arduino.h>
#include "midi.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
SerialStub Serial;
struct Port : Midi::Port {
  bool online=true, ok=true; uint32_t epoch=0; std::vector<Midi::Packet> packets;
  bool mounted() const override {return online;}
  bool ready() const override {return online;}
  uint32_t sessionEpoch() const override {return epoch;}
  bool send(const Midi::Packet& p) override {packets.push_back(p);return ok;}
};
int main() {
  Midi::Packet p;
  assert(Midi::encode(Midi::Message::noteOn(16,127,0),p));
  assert(p.bytes[0]==9 && p.bytes[1]==0x9f && p.bytes[2]==127 && p.bytes[3]==0);
  assert(Midi::encode(Midi::Message::controlChange(1,127,127),p) && p.bytes[0]==11 && p.bytes[1]==0xb0);
  for(unsigned i=0;i<128;++i) assert(Midi::encode(Midi::Message::programChange(16,i),p) && p.bytes[0]==12 && p.bytes[1]==0xcf && p.bytes[2]==i && p.bytes[3]==0);
  assert(!Midi::encode(Midi::Message::programChange(1,128),p));
  assert(!Midi::encode(Midi::Message::noteOn(0,60,100),p));
  assert(!Midi::encode(Midi::Message::noteOff(17,60),p));
  assert(!Midi::encode(Midi::Message::controlChange(1,128,0),p));
  assert(!Midi::encode(Midi::Message::controlChange(1,0,128),p));
  assert(!Midi::encode(Midi::Message::pulse(1,60,0),p));
  assert(!Midi::encode(Midi::Message::pulse(1,60,100,0),p));
  assert(!Midi::encode(Midi::Message::pulse(1,60,100,60001),p));
  Port port; Midi::Engine e(port); e.begin(); assert(port.packets.empty());
  auto pulse=Midi::Message::pulse(1,60,100);
  assert(e.submit(pulse,true,0)==Midi::Result::Submitted); e.update(99); assert(port.packets.size()==1);
  assert(e.submit(pulse,true,90)==Midi::Result::Submitted); e.update(100); assert(port.packets.size()==2);
  e.update(189); assert(e.pendingCount()==1); e.update(190);
  assert(port.packets.size()==3 && port.packets.back().bytes[1]==0x80 && e.pendingCount()==0);
  assert(e.submit(pulse,true,200)==Midi::Result::Submitted); port.ok=false;
  assert(e.submit(pulse,true,250)==Midi::Result::TransportFailure); port.ok=true;
  e.update(300); assert(e.pendingCount()==0); // failed refresh retained original deadline
  e.submit(pulse,true,UINT32_MAX-50); e.update(48); assert(e.pendingCount()==1);
  e.update(49); assert(e.pendingCount()==0);
  for(unsigned n=0;n<16;++n) assert(e.submit(Midi::Message::pulse(1,n,100),true,0)==Midi::Result::Submitted);
  size_t before=port.packets.size();
  assert(e.submit(Midi::Message::pulse(1,16,100),true,0)==Midi::Result::SchedulerFull && port.packets.size()==before);
  assert(e.submit(Midi::Message::pulse(1,0,100),true,50)==Midi::Result::Submitted); // refresh allowed when full
  e.submit(Midi::Message::noteOn(1,0,100),true,60); assert(e.pendingCount()==15);
  e.submit(Midi::Message::noteOff(1,1),true,60); assert(e.pendingCount()==14);
  port.online=false; e.update(61); assert(e.pendingCount()==0 && e.droppedReleases()==14);
  before=port.packets.size(); port.online=true; e.update(10000); assert(port.packets.size()==before);
  assert(e.submit(pulse,false,10000)==Midi::Result::Unavailable && port.packets.size()==before);
  e.submit(pulse,true,10001); ++port.epoch; e.update(10002); assert(e.pendingCount()==0); // reconnect between ticks
  e.submit(pulse,true,11000); port.ok=false; e.update(11100);
  assert(e.releaseFailures()==1 && e.lastRelease().result==Midi::Result::TransportFailure);
  before=port.packets.size(); e.update(20000); assert(port.packets.size()==before); // no retry
  puts("USB MIDI encoding/ranges, scheduler bounds, rollover, refresh, disconnect and release failure checks passed.");
}
