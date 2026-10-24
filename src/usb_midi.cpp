#include "usb_midi.h"
#include <USB.h>
#include <esp32-hal-tinyusb.h>
#include <string.h>

#if ARDUINO_USB_MODE || ARDUINO_USB_CDC_ON_BOOT || !CFG_TUD_MIDI || !CFG_TUD_CDC
#error "CueLink requires native TinyUSB, explicit CDC startup, and compiled MIDI+CDC drivers"
#endif
static UsbMidiPort* activePort = nullptr;
static uint16_t loadMidiDescriptor(uint8_t* destination, uint8_t* interface) {
  const uint8_t endpoint = tinyusb_get_free_duplex_endpoint();
  if (!endpoint) return 0;
  const uint8_t string = tinyusb_add_string_descriptor("CueLink Controller MIDI");
  const uint8_t descriptor[] = {TUD_MIDI_DESCRIPTOR(*interface, string, endpoint, uint8_t(endpoint | 0x80), 64)};
  memcpy(destination, descriptor, sizeof(descriptor));
  *interface += 2;  // AudioControl + MIDIStreaming.
  return sizeof(descriptor);
}
bool UsbMidiPort::begin() {
  if (initialized_) return true;
  if (tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, loadMidiDescriptor) != ESP_OK) return false;
  USB.productName("CueLink Controller");
  activePort = this;
  USB.onEvent([](void*, esp_event_base_t, int32_t event, void*) {
    if (event == ARDUINO_USB_STOPPED_EVENT || event == ARDUINO_USB_SUSPEND_EVENT) {
      activePort->epoch_.fetch_add(1);
    }
  });
  initialized_ = USB.begin();
  return initialized_;
}
bool UsbMidiPort::mounted() const { return initialized_ && tud_mounted(); }
bool UsbMidiPort::ready() const { return initialized_ && tud_ready() && tud_midi_mounted(); }
bool UsbMidiPort::send(const Midi::Packet& packet) { return ready() && tud_midi_packet_write(packet.bytes); }
