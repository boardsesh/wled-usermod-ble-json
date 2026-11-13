#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ble_json {
constexpr size_t MAX_MESSAGE = 8192;
constexpr uint32_t FRAGMENT_TIMEOUT_MS = 5000;
constexpr uint32_t IDLE_TIMEOUT_MS = 15000;
inline bool validUtf8(const uint8_t* bytes, size_t size) {
  for (size_t index=0;index<size;) {
    const uint8_t first=bytes[index++];
    const size_t width=first<0x80?1:first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
    if (!width || size-index<width-1) return false;
    uint32_t code=first & (width==1?0x7f:(1 << (7-width))-1);
    for (size_t offset=1;offset<width;++offset) { uint8_t next=bytes[index++]; if ((next&0xc0)!=0x80) return false; code=(code<<6)|(next&63); }
    if ((width==2&&code<0x80)||(width==3&&code<0x800)||(width==4&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff)) return false;
  }
  return true;
}
inline bool oneJsonObject(const uint8_t* bytes, size_t size) {
  size_t start=0;
  auto whitespace=[](uint8_t byte) { return byte==' '||byte=='\t'||byte=='\n'||byte=='\r'; };
  while (start<size && whitespace(bytes[start])) ++start;
  if (start==size || bytes[start]!='{') return false;
  bool quoted=false, escaped=false;
  size_t depth=0;
  for (size_t index=start;index<size;++index) {
    const uint8_t byte=bytes[index];
    if (quoted) {
      if (escaped) escaped=false;
      else if (byte=='\\') escaped=true;
      else if (byte=='"') quoted=false;
    } else if (byte=='"') quoted=true;
    else if (byte=='{'||byte=='[') ++depth;
    else if (byte=='}'||byte==']') {
      if (!depth) return false;
      if (--depth==0) {
        for (++index;index<size;++index) if (!whitespace(bytes[index])) return false;
        return true;
      }
    }
  }
  return false;
}
inline bool validId(const char* id) {
  if (!id || !*id || strlen(id)>64) return false;
  for (size_t index=0;id[index];++index) {
    const char character=id[index];
    if (!((character>='a'&&character<='z')||(character>='A'&&character<='Z')||(character>='0'&&character<='9')||character=='.'||character=='_'||character==':'||character=='-')) return false;
  }
  return true;
}
struct ReplyState {
  bool inFlight=false, waitingIndication=false, confirmed=false;
  size_t outgoingSize=0, outgoingOffset=0;
  void acknowledge() {
    if (waitingIndication && outgoingOffset==outgoingSize) {
      outgoingSize=outgoingOffset=0; inFlight=false;
      waitingIndication=confirmed=false;
    } else confirmed=true;
  }
};
// Single frame per operation. A second message is a protocol violation, never a queue.
class Frame {
 public:
  uint8_t bytes[MAX_MESSAGE + 4]{};
  size_t used = 0;
  size_t expected = 0;
  uint32_t lastFragment = 0;
  bool complete() const { return expected && used == expected + 4; }
  bool expired(uint32_t now) const { return used && uint32_t(now - lastFragment) >= FRAGMENT_TIMEOUT_MS; }
  void reset() { used = expected = lastFragment = 0; }
  bool append(const uint8_t* chunk, size_t length, uint32_t now) {
    if (!length || complete() || expired(now) || length > sizeof(bytes) - used) return false;
    memcpy(bytes + used, chunk, length);
    used += length;
    lastFragment = now;
    if (used >= 4) {
      expected = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
      if (!expected || expected > MAX_MESSAGE || used > expected + 4) return false;
    }
    return true;
  }
};
}
