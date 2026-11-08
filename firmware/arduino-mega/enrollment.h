#pragma once
#include <EEPROM.h>

// Separate EEPROM records, committed marker last. Never erase sensor templates.
struct __attribute__((packed)) CardMapping {
 uint8_t marker;
 char job[37];
 char uid[30];
 uint16_t fingerId;
 uint32_t checksum;
};
constexpr uint8_t MAPPING_LIMIT = 40;
uint32_t mappingChecksum(const CardMapping &m) {
 const uint8_t *bytes = reinterpret_cast<const uint8_t*>(&m);
 uint32_t hash=2166136261UL;
 for(size_t i=1;i<sizeof(m)-4;i++)hash=(hash^bytes[i])*16777619UL;
 return hash;
}
bool readMapping(uint8_t index,CardMapping &m) {
 EEPROM.get(index*sizeof(m),m);
 return m.marker==0xA7 && m.job[36]==0 && m.uid[29]==0 && m.fingerId>=2 && m.checksum==mappingChecksum(m);
}
uint16_t cardFinger(const String &uid) {
 if(uid==REGISTERED_UID)return REGISTERED_FINGER_ID;
 CardMapping m;
 for(uint8_t i=0;i<MAPPING_LIMIT;i++)if(readMapping(i,m)&&uid==m.uid)return m.fingerId;
 return 0;
}
void enrollmentStep(const char *job,const __FlashStringHelper *step) {
 Serial1.print(F("ENROLL_STEP,"));Serial1.print(job);Serial1.print(',');Serial1.println(step);
 Serial.print(F("ENROLL_STEP,"));Serial.print(job);Serial.print(',');Serial.println(step);
 Serial.print(F("ENROLLMENT: "));Serial.println(step);
}
void enrollmentFailure(const char *job,const __FlashStringHelper *error) {
 Serial1.print(F("ENROLL_FAIL,"));Serial1.print(job);Serial1.print(',');Serial1.println(error);
 Serial.print(F("ENROLL_FAIL,"));Serial.print(job);Serial.print(',');Serial.println(error);
 Serial.print(F("ENROLLMENT FAILED: "));Serial.println(error);
 lcdStatus(F("Enroll failed"),F("Check website"),2000);
}
void enrollmentDone(const CardMapping &m) {
 Serial1.print(F("ENROLL_DONE,"));Serial1.print(m.job);Serial1.print(',');Serial1.print(m.uid);Serial1.print(',');Serial1.println(m.fingerId);
 Serial.print(F("ENROLL_DONE,"));Serial.print(m.job);Serial.print(',');Serial.print(m.uid);Serial.print(',');Serial.println(m.fingerId);
 Serial.println(F("ENROLLMENT: local mapping saved; awaiting cloud activation."));
 lcdStatus(F("Enrollment saved"),F("Wait for cloud"),2000);
}
bool captureEnrollmentImage(uint8_t buffer) {
 const unsigned long start=millis();
 while(millis()-start<30000){
  const uint8_t result=finger.getImage();
  if(result==FINGERPRINT_OK)return finger.image2Tz(buffer)==FINGERPRINT_OK;
  if(result!=FINGERPRINT_NOFINGER)return false;
  delay(60);
 }
 return false;
}
// 0x1F returns the occupancy bitmap. Unsupported/error replies stop enrollment.
// Protocol reference: Adafruit CircuitPython Fingerprint.read_templates().
uint16_t freeSensorSlot() {
 if(finger.getParameters()!=FINGERPRINT_OK)return 0;
 for(uint8_t page=0;page<4 && (uint16_t)page*256<finger.capacity;page++){
  uint8_t data[]={0x1F,page};
  Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET,sizeof(data),data);
  finger.writeStructuredPacket(packet);
  if(finger.getStructuredPacket(&packet)!=FINGERPRINT_OK || packet.type!=FINGERPRINT_ACKPACKET || packet.length!=35 || packet.data[0]!=FINGERPRINT_OK)return 0;
  uint16_t sum=FINGERPRINT_ACKPACKET+35;
  for(uint8_t i=0;i<33;i++)sum+=packet.data[i];
  if(sum!=((uint16_t)packet.data[33]<<8 | packet.data[34]))return 0;
  for(uint16_t bit=0;bit<256;bit++){
   const uint16_t slot=(uint16_t)page*256+bit;
   if(slot>=2 && slot<finger.capacity && !(packet.data[1+bit/8] & (1<<(bit%8))))return slot;
  }
 }
 return 0;
}
void enrollEmployee(const char *job) {
 CardMapping m;int8_t freeRecord=-1;
 for(uint8_t i=0;i<MAPPING_LIMIT;i++){
  if(readMapping(i,m) && strcmp(m.job,job)==0){enrollmentDone(m);return;}
  if(EEPROM.read(i*sizeof(m))==0xFF && freeRecord<0)freeRecord=i;
 }
 if(!fingerprintReady){enrollmentFailure(job,F("SENSOR_UNAVAILABLE"));return;}
 if(freeRecord<0){enrollmentFailure(job,F("LOCAL_MAPPING_FULL"));return;}
 const uint16_t slot=freeSensorSlot();
 if(!slot){enrollmentFailure(job,F("NO_SAFE_SENSOR_SLOT"));return;}
 enrollmentStep(job,F("tap_card"));lcdStatus(F("Enroll employee"),F("Tap NEW card"));
 const unsigned long start=millis();String uid;
 while(millis()-start<45000){
  if(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()){
   for(byte i=0;i<rfid.uid.size;i++){if(i)uid+=' ';if(rfid.uid.uidByte[i]<16)uid+='0';uid+=String(rfid.uid.uidByte[i],HEX);}uid.toUpperCase();
   rfid.PICC_HaltA();rfid.PCD_StopCrypto1();break;
  }
  delay(30);
 }
 if(uid.length()!=11 && uid.length()!=20 && uid.length()!=29){enrollmentFailure(job,F("CARD_TIMEOUT"));return;}
 if(cardFinger(uid)){enrollmentFailure(job,F("CARD_ALREADY_REGISTERED"));return;}
 enrollmentStep(job,F("place_finger"));lcdStatus(F("Enroll finger"),F("Place finger"));
 if(!captureEnrollmentImage(1)){enrollmentFailure(job,F("FIRST_CAPTURE_FAILED"));return;}
 const uint8_t search=finger.fingerSearch(1);
 if(search==FINGERPRINT_OK){enrollmentFailure(job,F("FINGER_ALREADY_REGISTERED"));return;}
 if(search!=FINGERPRINT_NOTFOUND){enrollmentFailure(job,F("SENSOR_SEARCH_FAILED"));return;}
 enrollmentStep(job,F("remove_finger"));lcdStatus(F("First scan OK"),F("Lift finger"));
 const unsigned long removal=millis();bool lifted=false;
 while(millis()-removal<15000){if(finger.getImage()==FINGERPRINT_NOFINGER){lifted=true;break;}delay(60);}
 if(!lifted){enrollmentFailure(job,F("REMOVE_FINGER_TIMEOUT"));return;}
 enrollmentStep(job,F("place_again"));lcdStatus(F("Same finger"),F("Place again"));
 if(!captureEnrollmentImage(2)){enrollmentFailure(job,F("SECOND_CAPTURE_FAILED"));return;}
 if(finger.createModel()!=FINGERPRINT_OK){enrollmentFailure(job,F("FINGERPRINT_MISMATCH"));return;}
 enrollmentStep(job,F("saving"));
 if(finger.storeModel(slot)!=FINGERPRINT_OK){enrollmentFailure(job,F("SENSOR_SAVE_FAILED"));return;}
 memset(&m,0,sizeof(m));m.marker=0xA7;strncpy(m.job,job,36);uid.toCharArray(m.uid,sizeof(m.uid));m.fingerId=slot;m.checksum=mappingChecksum(m);
 const int address=freeRecord*sizeof(m);const uint8_t *bytes=reinterpret_cast<const uint8_t*>(&m);
 EEPROM.update(address,0);for(size_t i=1;i<sizeof(m);i++)EEPROM.update(address+i,bytes[i]);EEPROM.update(address,0xA7);
 CardMapping checked;
 if(!readMapping(freeRecord,checked)){enrollmentFailure(job,F("LOCAL_SAVE_FAILED"));return;}
 enrollmentDone(checked);lastRFIDScan=millis();
}
struct EnrollmentCommandReader { char line[64]; uint8_t size=0; bool discard=false; };
void readEnrollmentCommands(Stream &port,EnrollmentCommandReader &reader) {
 char *line=reader.line;uint8_t &size=reader.size;bool &discard=reader.discard;
 while(port.available()){
  char c=port.read();if(c=='\r')continue;
  if(c=='\n'){
   line[size]=0;
   if(!discard && size==43 && strncmp(line,"ENROLL,",7)==0){
    bool valid=true;for(uint8_t i=0;i<36;i++){const char ch=line[7+i];const bool dash=i==8||i==13||i==18||i==23;if(dash?ch!='-':!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')))valid=false;}
    if(valid){
     Serial.println(F("ENROLLMENT: command received from ESP32."));
     Serial1.print(F("ENROLL_ACK,"));Serial1.print(line+7);Serial1.println(F(",received"));
     Serial.print(F("ENROLL_ACK,"));Serial.print(line+7);Serial.println(F(",received"));
     enrollEmployee(line+7);
     Serial.println(F("SYSTEM READY"));
    }
   }
   size=0;discard=false;
  }else if(!discard){if(size<sizeof(reader.line)-1)line[size++]=c;else discard=true;}
 }
}
void readBridgeCommands() {
 static EnrollmentCommandReader uartReader,usbReader;
 readEnrollmentCommands(Serial1,uartReader);
 readEnrollmentCommands(Serial,usbReader);
}
