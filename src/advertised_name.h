#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace ble_json {
// Bytes left for the owner's name in the scan response: 31 minus the AD header (2),
// "#" and the 6-character serial, so the whole name always fits.
constexpr size_t NAME_BYTES = 22;
constexpr size_t SERIAL_CHARS = 6;

// Length of the UTF-8 sequence that starts with lead, or 0 when it cannot start one.
inline size_t utf8Length(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xc2 && lead <= 0xdf) return 2;
  if (lead >= 0xe0 && lead <= 0xef) return 3;
  if (lead >= 0xf0 && lead <= 0xf4) return 4;
  return 0;
}

// "<name>#<SERIAL>", the shape the app's board picker already reads ("Kilter Board#751737@3").
// The name keeps whole UTF-8 characters within NAME_BYTES. "#" and "@" delimit the serial,
// so they are dropped from the name, as are control characters and invalid bytes. The serial
// is the last SERIAL_CHARS of the controller ID ("wled-1cc3abc0559c" -> "C0559C").
inline std::string advertisedName(const char* name, const char* controllerId) {
  std::string label;
  const auto* bytes = reinterpret_cast<const uint8_t*>(name ? name : "");
  for (size_t index = 0; bytes[index];) {
    const size_t length = utf8Length(bytes[index]);
    bool valid = length > 0;
    for (size_t next = 1; valid && next < length; ++next) valid = (bytes[index + next] & 0xc0) == 0x80;
    if (!valid) { ++index; continue; }
    const char lead = static_cast<char>(bytes[index]);
    const bool dropped = length == 1 && (lead == '#' || lead == '@' || bytes[index] < 0x20 || bytes[index] == 0x7f);
    if (!dropped) {
      if (label.size() + length > NAME_BYTES) break;
      label.append(reinterpret_cast<const char*>(bytes + index), length);
    }
    index += length;
  }
  while (!label.empty() && label.back() == ' ') label.pop_back();
  size_t start = 0;
  while (start < label.size() && label[start] == ' ') ++start;
  label.erase(0, start);
  if (label.empty()) label = "WLED";
  std::string serial;
  const std::string id = controllerId ? controllerId : "";
  const std::string tail = id.size() >= SERIAL_CHARS ? id.substr(id.size() - SERIAL_CHARS) : id;
  for (const char character : tail) serial += (character >= 'a' && character <= 'f') ? character - 'a' + 'A' : character;
  return label + "#" + serial;
}
}
