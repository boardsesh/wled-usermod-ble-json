#include "wled.h"
#include <NimBLEDevice.h>
#include <atomic>
#include "frame.h"
#include "lighting.h"

#ifndef BLE_JSON_DEFAULT_ENABLED
#define BLE_JSON_DEFAULT_ENABLED 0
#endif
#ifndef BLE_JSON_AUDIO
#define BLE_JSON_AUDIO 0
#endif
#ifndef BLE_JSON_BUILD_ID
#define BLE_JSON_BUILD_ID "ble-json-0.1.0"
#endif

namespace {
constexpr char SERVICE[] = "2f8d0001-6f6e-4c9d-9b74-8a3d41c3a201";
constexpr char CAPABILITIES[] = "2f8d0002-6f6e-4c9d-9b74-8a3d41c3a201";
constexpr char REQUEST[] = "2f8d0003-6f6e-4c9d-9b74-8a3d41c3a201";
constexpr char RESPONSE[] = "2f8d0004-6f6e-4c9d-9b74-8a3d41c3a201";
constexpr uint16_t NO_PEER = BLE_HS_CONN_HANDLE_NONE;


}

class BleJsonUsermod : public Usermod, public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks, private ble_json::ReplyState {
 private:
  std::atomic<bool> enabled{BLE_JSON_DEFAULT_ENABLED};
  bool started = false;
  std::atomic<bool> updating{false};
  SemaphoreHandle_t mutex = nullptr;
  SemaphoreHandle_t lifecycle = nullptr;
  struct LifecycleGuard {
    SemaphoreHandle_t semaphore;
    explicit LifecycleGuard(SemaphoreHandle_t mutex):semaphore(mutex) { xSemaphoreTake(semaphore,portMAX_DELAY); }
    ~LifecycleGuard() { xSemaphoreGive(semaphore); }
  };
  NimBLEServer* server = nullptr;
  NimBLECharacteristic* capabilities = nullptr;
  NimBLECharacteristic* response = nullptr;
  uint16_t peer = NO_PEER;
  bool subscribed = false;
  bool closing = false;
  bool failed = false;
  uint32_t lastActivity = 0;
  uint32_t generation = 0;
  ble_json::Frame request;
  uint8_t outgoing[ble_json::MAX_MESSAGE + 4]{};
  char controllerId[18]{};
  uint32_t capabilityRefresh=0;

  void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mutex); }
  void discard() {
    request.reset(); outgoingSize=outgoingOffset=0;
    inFlight=waitingIndication=confirmed=failed=false;
  }
  void info(JsonObject root) {
    root["controllerId"] = controllerId;
    root["protocolVersion"] = 1;
    root["usermodVersion"] = "0.1.0";
    root["buildId"] = BLE_JSON_BUILD_ID;
    root["name"] = serverDescription;
    root["wledVersion"] = versionString;
    root["ledCount"] = strip.getLengthTotal();
    root["audio"] = bool(BLE_JSON_AUDIO);
    root["maxMessageBytes"] = ble_json::MAX_MESSAGE;
  }
  void refreshCapabilities() {
    DynamicJsonDocument document(768);
    JsonObject root = document.to<JsonObject>(); info(root);
    String json; serializeJson(document,json); capabilities->setValue(reinterpret_cast<const uint8_t*>(json.c_str()),json.length());
  }
  void start() {
    if (started || !enabled || updating) return;
    NimBLEDevice::init("WLED BLE JSON");
    NimBLEDevice::setSecurityAuth(false,false,false);
    NimBLEDevice::setMTU(247);
    server = NimBLEDevice::createServer();
    server->setCallbacks(this,false);
    server->advertiseOnDisconnect(false);
    NimBLEService* service = server->createService(SERVICE);
    capabilities = service->createCharacteristic(CAPABILITIES,NIMBLE_PROPERTY::READ,768);
    auto* incoming = service->createCharacteristic(REQUEST,NIMBLE_PROPERTY::WRITE,512);
    response = service->createCharacteristic(RESPONSE,NIMBLE_PROPERTY::INDICATE,512);
    incoming->setCallbacks(this); response->setCallbacks(this);
    refreshCapabilities(); service->start();
    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE);
    advertising->start(); started=true;
  }
  void stop() {
    if (!started) return;
    lock(); closing=true; discard(); uint16_t connection=peer; unlock();
    NimBLEDevice::getAdvertising()->stop();
    if (connection != NO_PEER) server->disconnect(connection);
    // deinit joins the host task before any shared objects may be reused.
    NimBLEDevice::deinit(true);
    lock(); peer=NO_PEER; subscribed=false; started=false; ++generation; discard(); unlock();
  }
  void encode(JsonDocument& document) {
    const size_t length=measureJson(document);
    if (!length || length > ble_json::MAX_MESSAGE || document.overflowed()) { closing=true; return; }
    for (size_t index=0;index<4;++index) outgoing[index]=(length >> (8*index)) & 255;
    serializeJson(document,outgoing+4,ble_json::MAX_MESSAGE);
    outgoingSize=length+4; outgoingOffset=0;
  }
  void error(JsonDocument& document, const char* id, const char* code) {
    document.clear(); document["id"]=id;
    document["error"]["code"]=code; encode(document);
  }
  // Called only on WLED's main task, under the shared JSON lock and connection mutex.
  void apply() {
    DynamicJsonDocument document(32768);
    if (!ble_json::validUtf8(request.bytes+4,request.expected) || !ble_json::oneJsonObject(request.bytes+4,request.expected)) { request.reset(); error(document,"","invalid-json"); return; }
    auto parseError=deserializeJson(document,request.bytes+4,request.expected,DeserializationOption::NestingLimit(12));
    request.reset();
    if (parseError || !document.is<JsonObject>()) { error(document,"","invalid-json"); return; }
    JsonObject root=document.as<JsonObject>();
    const char* rawId=root["id"];
    if (!ble_json::validId(rawId)) { error(document,"","invalid-request"); return; }
    char id[65]; strlcpy(id,rawId,sizeof(id));
    const char* op=root["op"];
    bool valid=true;
    for (JsonPair entry:root) if (strcmp(entry.key().c_str(),"id") && strcmp(entry.key().c_str(),"op") && strcmp(entry.key().c_str(),"state")) valid=false;
    if (!valid || !op) { error(document,id,"invalid-request"); return; }
    if (!strcmp(op,"setState")) {
      if (!root["state"].is<JsonObject>() || !ble_json_validation::lighting(root["state"].as<JsonObjectConst>(),strip.getLengthTotal())) { error(document,id,"disallowed-state"); return; }
      for (JsonObjectConst segment:root["state"]["seg"].as<JsonArrayConst>()) {
        if (segment["id"].as<uint32_t>()>=strip.getSegmentsNum()) { error(document,id,"invalid-segment"); return; }
      }
      // WLED's boolean return asks for a verbose response; it is NOT success/failure.
      deserializeState(root["state"].as<JsonObject>(),CALL_MODE_DIRECT_CHANGE);
      document.clear(); document["id"]=id; document["result"]["success"]=true;
    } else if (!strcmp(op,"getInfo") && !root.containsKey("state")) {
      document.clear(); document["id"]=id;
      JsonObject result=document.createNestedObject("result");
      result["name"]=serverDescription; result["ver"]=versionString; result["leds"]["count"]=strip.getLengthTotal();
    } else if (!strcmp(op,"getState") && !root.containsKey("state")) {
      document.clear(); document["id"]=id;
      JsonObject result=document.createNestedObject("result");
      // Build a sanitized runtime snapshot directly; never expose usermod data, preset names, or credentials.
      result["on"]=bri>0; result["bri"]=bri;
      result["nl"]["on"]=nightlightActive;
      JsonArray segments=result.createNestedArray("seg");
      for (size_t index=0;index<strip.getSegmentsNum();++index) {
        const auto& segment=strip.getSegment(index);
        if (!segment.isActive()) continue;
        JsonObject item=segments.createNestedObject(); item["id"]=index;
        item["start"]=segment.start; item["stop"]=segment.stop;
        item["on"]=bool(segment.on); item["bri"]=segment.opacity; item["fx"]=segment.mode;
      }
    } else { error(document,id,"invalid-operation"); return; }
    encode(document);
  }
 public:
  void setup() override {
    mutex=xSemaphoreCreateMutex(); lifecycle=xSemaphoreCreateMutex(); if (!mutex || !lifecycle) { enabled=false; return; }
    const uint64_t mac=ESP.getEfuseMac();
    // Same byte order as esp_efuse_mac_get_default / ESP32 station MAC.
    snprintf(controllerId,sizeof(controllerId),"wled-%02x%02x%02x%02x%02x%02x",unsigned(mac&255),unsigned((mac>>8)&255),unsigned((mac>>16)&255),unsigned((mac>>24)&255),unsigned((mac>>32)&255),unsigned((mac>>40)&255));
    start();
  }
  void onConnect(NimBLEServer* connectedServer,NimBLEConnInfo& connection) override {
    lock();
    if (peer!=NO_PEER || updating || !enabled) { unlock(); connectedServer->disconnect(connection.getConnHandle()); return; }
    peer=connection.getConnHandle(); ++generation; discard(); closing=false; subscribed=false; lastActivity=millis();
    unlock();
  }
  void onDisconnect(NimBLEServer*,NimBLEConnInfo& connection,int) override {
    lock();
    if (peer==connection.getConnHandle()) { peer=NO_PEER; ++generation; subscribed=false; discard(); }
    unlock();
    // Advertising is restarted by the main loop only, never during OTA teardown.
  }
  void onSubscribe(NimBLECharacteristic*,NimBLEConnInfo& connection,uint16_t subscription) override {
    lock(); if (peer==connection.getConnHandle()) subscribed=(subscription&2)!=0; unlock();
  }
  void onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& connection) override {
    auto chunk=characteristic->getValue();
    lock();
    if (peer!=connection.getConnHandle() || closing || !subscribed || inFlight || !request.append(chunk.data(),chunk.size(),millis())) closing=true;
    else { lastActivity=millis(); if (request.complete()) inFlight=true; }
    unlock();
  }
  void onStatus(NimBLECharacteristic*,NimBLEConnInfo& connection,int status) override {
    lock(); if (peer==connection.getConnHandle()) {
      if (status==BLE_HS_EDONE) {
        lastActivity=millis();
        acknowledge();
      }
      else if (status!=0) failed=true;
    } unlock();
  }
  void loop() override {
    if (!mutex || !lifecycle) return;
    LifecycleGuard lifecycleGuard(lifecycle);
    if (!enabled || updating) { stop(); return; }
    start();
    if (uint32_t(millis()-capabilityRefresh)>=1000) { refreshCapabilities(); capabilityRefresh=millis(); }
    lock();
    if (peer==NO_PEER) { unlock(); if (!NimBLEDevice::getAdvertising()->isAdvertising()) NimBLEDevice::getAdvertising()->start(); return; }
    const uint32_t now=millis();
    if (request.expired(now) || uint32_t(now-lastActivity)>=ble_json::IDLE_TIMEOUT_MS || failed) closing=true;
    if (closing) { uint16_t connection=peer; discard(); unlock(); server->disconnect(connection); return; }
    if (inFlight && request.complete()) {
      // Never wait while holding WLED's JSON buffer: defer to next iteration.
      if (requestJSONBufferLock()) { apply(); releaseJSONBufferLock(); }
    }
    if (waitingIndication && confirmed) {
      waitingIndication=false; confirmed=false;
      if (outgoingOffset==outgoingSize) { outgoingSize=outgoingOffset=0; inFlight=false; }
    }
    if (outgoingSize && !waitingIndication && !closing) {
      const uint16_t mtu=server->getPeerMTU(peer);
      const size_t length=std::min(outgoingSize-outgoingOffset,size_t(mtu>=23 ? mtu-3 : 20));
      const uint16_t connection=peer;
      const uint32_t operationGeneration=generation;
      uint8_t chunk[512]; memcpy(chunk,outgoing+outgoingOffset,length);
      waitingIndication=true; outgoingOffset+=length;
      unlock();
      const bool sent=response->indicate(chunk,length,connection);
      lock(); if (!sent && generation==operationGeneration) closing=true;
    }
    unlock();
  }
  void onUpdateBegin(bool begin) override {
    updating=begin;
    if (!lifecycle) return;
    LifecycleGuard lifecycleGuard(lifecycle);
    if (begin) stop();
  }
  void addToJsonInfo(JsonObject& root) override {
    JsonObject capability=root.createNestedObject("bleJson"); info(capability); capability["enabled"]=bool(enabled);
  }
  void addToConfig(JsonObject& root) override { root["BLE JSON"]["enabled"]=bool(enabled); }
  bool readFromConfig(JsonObject& root) override {
    JsonVariant setting=root["BLE JSON"]["enabled"];
    if (setting.is<bool>()) { enabled=setting.as<bool>(); return true; }
    // Missing key only: first install default differs between generic/gym images.
    enabled=BLE_JSON_DEFAULT_ENABLED; return false;
  }
};
static BleJsonUsermod bleJsonUsermod;
REGISTER_USERMOD(bleJsonUsermod);
