#include <cassert>
#include <atomic>
#include <fstream>
#include <iostream>
#include <iterator>
#include "ArduinoJson-v6.h"
#include "frame.h"
#include "lighting.h"
#include "preserved_config.h"
using ble_json::Frame;

bool allows(const char* json, uint32_t count = 1000) {
  DynamicJsonDocument document(32768);
  assert(!deserializeJson(document,json));
  return ble_json_validation::lighting(document.as<JsonObjectConst>(),count);
}
void preservedBleEnabled() {
  for (bool firstInstallDefault : {false,true}) {
    std::atomic<bool> enabled{firstInstallDefault};
    DynamicJsonDocument config(1024);
    assert(!deserializeJson(config,"{}"));
    assert(!ble_json::readBleEnabled(config.as<JsonObjectConst>(),enabled));
    assert(enabled.load()==firstInstallDefault);
    assert(!deserializeJson(config,R"({"BLE JSON":{"enabled":false}})"));
    assert(ble_json::readBleEnabled(config.as<JsonObjectConst>(),enabled));
    assert(!enabled.load());
    for (const char* partial : {R"({"AudioReactive":{"enabled":true}})",R"({"BLE JSON":{}})",R"({"BLE JSON":{"enabled":"true"}})"}) {
      assert(!deserializeJson(config,partial));
      assert(!ble_json::readBleEnabled(config.as<JsonObjectConst>(),enabled));
      assert(!enabled.load());
    }
    assert(!deserializeJson(config,R"({"BLE JSON":{"enabled":true}})"));
    assert(ble_json::readBleEnabled(config.as<JsonObjectConst>(),enabled));
    assert(enabled.load());
    assert(!deserializeJson(config,"{}"));
    assert(!ble_json::readBleEnabled(config.as<JsonObjectConst>(),enabled));
    assert(enabled.load());
  }
}
void preservedAudioConfig() {
  DynamicJsonDocument original(4096), partial(1024), output(4096), reloaded(4096);
  assert(!deserializeJson(original,R"({"BLE JSON":{"enabled":false},"AudioReactive":{"enabled":false,"digitalmic":{"type":1,"pin":[32,15,14]},"analogmic":{"pin":36},"config":{"squelch":10,"gain":30},"label":"saved microphone"}})"));
  std::string stored;
  ble_json::rememberDormantAudio(original.as<JsonObjectConst>(),stored);
  assert(!stored.empty());
  assert(!deserializeJson(partial,R"({"BLE JSON":{"enabled":false}})"));
  ble_json::rememberDormantAudio(partial.as<JsonObjectConst>(),stored);
  JsonObject saved=output.to<JsonObject>();
  saved["BLE JSON"]["enabled"]=false;
  ble_json::restoreDormantAudio(saved,stored);
  std::string persisted;
  serializeJson(output,persisted);
  assert(!deserializeJson(reloaded,persisted));
  assert(reloaded["BLE JSON"]["enabled"].is<bool>() && !reloaded["BLE JSON"]["enabled"].as<bool>());
  assert(reloaded["AudioReactive"].is<JsonObject>());
  assert(reloaded["AudioReactive"]==original["AudioReactive"]);
  // A subsequent boot and save must retain the same complete dormant object.
  std::string afterReboot;
  ble_json::rememberDormantAudio(reloaded.as<JsonObjectConst>(),afterReboot);
  assert(afterReboot==stored);
  // An explicit replacement updates the dormant configuration rather than merging old pins.
  assert(!deserializeJson(partial,R"({"AudioReactive":{"enabled":true,"digitalmic":{"type":2,"pin":[4,5,6]}}})"));
  ble_json::rememberDormantAudio(partial.as<JsonObjectConst>(),stored);
  output.clear(); saved=output.to<JsonObject>();
  ble_json::restoreDormantAudio(saved,stored);
  persisted.clear(); serializeJson(output,persisted);
  assert(!deserializeJson(reloaded,persisted));
  assert(reloaded["AudioReactive"]==partial["AudioReactive"]);
  // Fresh no-audio installs must not invent microphone settings.
  output.clear(); saved=output.to<JsonObject>();
  ble_json::restoreDormantAudio(saved,std::string{});
  assert(!saved.containsKey("AudioReactive"));
}
int main(int argc,char** argv) {
  preservedBleEnabled();
  preservedAudioConfig();
  const uint8_t json[] = {2,0,0,0,'{','}'};
  for (size_t size=1;size<=6;++size) {
    Frame frame;
    for (size_t offset=0;offset<6;offset+=size) assert(frame.append(json+offset,std::min(size,6-offset),10));
    assert(frame.complete()); assert(!frame.append(json,6,20));
    frame.reset(); assert(!frame.complete());
    assert(frame.append(json,3,100)); assert(!frame.append(json+3,3,5100));
  }
  ble_json::ReplyState reply;
  reply.inFlight=true; reply.waitingIndication=true; reply.outgoingSize=100; reply.outgoingOffset=100;
  reply.acknowledge(); // next request is legal before main loop gets another turn
  assert(!reply.inFlight && !reply.waitingIndication && !reply.outgoingSize);
  reply.inFlight=true; reply.waitingIndication=true; reply.outgoingSize=100; reply.outgoingOffset=20;
  reply.acknowledge(); assert(reply.inFlight && reply.confirmed);
  const char* validJson=" {\"state\":{\"on\":true}} ";
  const char* trailingJson="{\"on\":true}garbage";
  assert(ble_json::oneJsonObject(reinterpret_cast<const uint8_t*>(validJson),strlen(validJson)));
  assert(!ble_json::oneJsonObject(reinterpret_cast<const uint8_t*>(trailingJson),strlen(trailingJson)));
  assert(ble_json::validId("read:1")); assert(!ble_json::validId("bad id"));
  const uint8_t invalidUtf8[]={0xc0,0x80}; assert(!ble_json::validUtf8(invalidUtf8,2));
  Frame frame;
  const uint8_t zero[] = {0,0,0,0}, large[] = {1,32,0,0}, overflow[] = {1,0,0,0,'{','}'};
  assert(!frame.append(zero,4,1)); frame.reset(); assert(!frame.append(large,4,1)); frame.reset(); assert(!frame.append(overflow,6,1)); frame.reset();
  assert(frame.append(json,2,0xfffffff0)); assert(frame.expired(0xfffffff0u+5000u));
  assert(allows("{\"on\":true,\"bri\":64,\"seg\":[{\"id\":0,\"start\":0,\"stop\":1000,\"i\":[0,1000,\"00000000\",999,\"FF000000\"]}]}"));
  assert(!allows("{\"on\":true,\"psave\":1}"));
  assert(!allows("{\"on\":true,\"wifi\":{\"ap\":true}}"));
  assert(!allows("{\"seg\":[{\"id\":0,\"i\":[1000,\"ffffff\"]}]}"));
  assert(!allows("{\"seg\":[{\"id\":0,\"i\":[0,1001,\"ffffff\"]}]}"));
  assert(!allows("{\"bri\":true}")); assert(!allows("{\"bri\":1.5}")); assert(!allows("{\"live\":true}"));
  assert(!allows("{\"nl\":{\"on\":false,\"dur\":10}}"));
  assert(!allows("{\"seg\":[{\"id\":0,\"col\":[[256,0,0]]}]}"));
  if (argc>1) {
    std::ifstream input(argv[1]); std::string vectors((std::istreambuf_iterator<char>(input)),{});
    DynamicJsonDocument document(65536); assert(!deserializeJson(document,vectors));
    size_t vectorCount=0;
    for (JsonObject vector:document["vectors"].as<JsonArray>()) {
      const char* hex=vector["frameHex"]; if (!hex) continue;
      Frame framed;
      for (size_t offset=0;offset<strlen(hex);offset+=2) {
        const std::string octet(hex+offset,2); uint8_t byte=std::stoul(octet,nullptr,16); assert(framed.append(&byte,1,100));
      }
      assert(framed.complete()); ++vectorCount;
      DynamicJsonDocument decoded(8192); assert(!deserializeJson(decoded,framed.bytes+4,framed.expected));
      assert(decoded.as<JsonVariant>()==vector["message"]);
    }
    assert(vectorCount>=5);
  }
  std::cout << "frame and lighting host tests passed\n";
}
