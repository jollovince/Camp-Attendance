#pragma once
#include <ArduinoJson.h>

struct EnrollmentState {
 uint32_t version;
 char job[37];
 char step[24];
 char uid[30];
 char error[64];
 uint16_t finger;
 bool result;
};
EnrollmentState enrollment{};
Preferences enrollmentStorage;
bool enrollmentStorageReady=false;
bool megaEnrollmentConfirmed=false;
unsigned long firstEnrollmentCommand=0;
unsigned long nextEnrollmentPoll=0,lastEnrollmentCommand=0;
constexpr unsigned long ENROLLMENT_POLL_MS=1000;

bool saveEnrollment(const EnrollmentState &next) {
 if(!enrollmentStorageReady || enrollmentStorage.putBytes("state",&next,sizeof(next))!=sizeof(next)){
  Serial.println("ENROLLMENT ERROR: flash save failed; result retained for retry.");return false;
 }
 enrollment=next;return true;
}
void beginEnrollmentBridge() {
 enrollmentStorageReady=enrollmentStorage.begin("camp-enroll",false);
 if(enrollmentStorageReady && enrollmentStorage.isKey("state")){
  if(enrollmentStorage.getBytes("state",&enrollment,sizeof(enrollment))!=sizeof(enrollment) || enrollment.version!=1 || enrollment.job[36] || enrollment.uid[29] || enrollment.step[23] || enrollment.error[63]){
   enrollmentStorageReady=false;memset(&enrollment,0,sizeof(enrollment));Serial.println("ENROLLMENT ERROR: saved state invalid; preserved for recovery.");
  }
 }
}
bool handleEnrollmentLine(const char *line) {
 if(strncmp(line,"ENROLL_",7)!=0)return false;
 char copy[192];strlcpy(copy,line,sizeof(copy));char *context=nullptr;
 const char *kind=strtok_r(copy,",",&context),*job=strtok_r(nullptr,",",&context),*value=strtok_r(nullptr,",",&context);
 if(!job||!value||strcmp(job,enrollment.job)||enrollment.result)return true;
 EnrollmentState next=enrollment;next.version=1;
 if(strcmp(kind,"ENROLL_ACK")==0){
  if(strcmp(value,"received"))return true;
  megaEnrollmentConfirmed=true;
  Serial.println("ENROLLMENT: Mega acknowledged command.");
 }else if(strcmp(kind,"ENROLL_STEP")==0){
  if(strcmp(value,"tap_card")&&strcmp(value,"place_finger")&&strcmp(value,"remove_finger")&&strcmp(value,"place_again")&&strcmp(value,"saving"))return true;
  strlcpy(enrollment.step,value,sizeof(enrollment.step));
  megaEnrollmentConfirmed=true;
  Serial.printf("ENROLLMENT: %s\n",value);lastEnrollmentCommand=millis();nextEnrollmentPoll=0;
 }else if(strcmp(kind,"ENROLL_FAIL")==0){
  for(const char *p=value;*p;p++)if(!((*p>='A'&&*p<='Z')||*p=='_'))return true;
  strlcpy(next.step,"failed",sizeof(next.step));strlcpy(next.error,value,sizeof(next.error));next.result=true;
  if(!saveEnrollment(next))enrollment=next;
  Serial.printf("ENROLLMENT FAILED: %s\n",value);nextEnrollmentPoll=0;
 }else if(strcmp(kind,"ENROLL_DONE")==0){
  const char *number=strtok_r(nullptr,",",&context);if(!number||strtok_r(nullptr,",",&context))return true;
  String verification=String("ATTENDANCE_VERIFIED,EMPLOYEE_ID:1,RFID_UID:")+value+",FINGERPRINT_ID:"+number;
  HardwareScan scan{};if(!parseHardwareScan(verification.c_str(),scan)||scan.fingerprintId<2)return true;
  strlcpy(next.uid,scan.uid,sizeof(next.uid));next.finger=scan.fingerprintId;strlcpy(next.step,"completed",sizeof(next.step));next.result=true;
  if(!saveEnrollment(next))enrollment=next;
  Serial.println("ENROLLMENT: hardware saved; waiting for cloud activation.");nextEnrollmentPoll=0;
 }
 return true;
}
void pollEnrollment() {
 if(!enrollmentStorageReady || (long)(millis()-nextEnrollmentPoll)<0 || WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000)return;
 nextEnrollmentPoll=millis()+ENROLLMENT_POLL_MS;
 if(!strlen(INGEST_URL)||strlen(DEVICE_TOKEN)<32)return;
 String endpoint=INGEST_URL;const int slash=endpoint.lastIndexOf('/');if(slash<8||!endpoint.startsWith("https://"))return;
 endpoint=endpoint.substring(0,slash+1)+"device-enrollment";
 JsonDocument payload;payload["deviceId"]=DEVICE_ID;
 if(enrollment.job[0]){
  payload["jobId"]=enrollment.job;
  payload["step"]=enrollment.result?enrollment.step:
    (megaEnrollmentConfirmed && strcmp(enrollment.step,"waiting_for_mega") ? enrollment.step : "waiting_for_mega");
  if(enrollment.result){
   // Retry flash persistence before submitting the result; never depend on RAM only.
   if(!saveEnrollment(enrollment))return;
   if(enrollment.finger){payload["uid"]=enrollment.uid;payload["finger"]=enrollment.finger;}
   else payload["error"]=enrollment.error;
  }
 }
 String body;serializeJson(payload,body);
 NetworkClientSecure tls;tls.useBuiltinCACertBundle();tls.setHandshakeTimeout(5);
 HTTPClient http;http.setConnectTimeout(3000);http.setTimeout(3000);
 if(!http.begin(tls,endpoint))return;
 http.addHeader("Content-Type","application/json");http.addHeader("Authorization",String("Bearer ")+DEVICE_TOKEN);
 const int code=http.POST(body);String response=code==200?http.getString():"";http.end();
 if(code!=200){Serial.printf("ENROLLMENT: server request failed (HTTP %d); retrying.\n",code);nextEnrollmentPoll=millis()+10000;return;}
 JsonDocument receipt;if(deserializeJson(receipt,response))return;
 if(enrollment.job[0]){
  if(receipt["jobId"].as<String>()!=enrollment.job)return;
  if(receipt["ack"]==true){
   const String state=receipt["state"].as<String>();
   if(state!="completed"&&state!="failed"&&state!="expired")return;
   Serial.printf("ENROLLMENT SERVER: %s\n",state.c_str());
   EnrollmentState empty{};empty.version=1;if(!saveEnrollment(empty))return;
   lastEnrollmentCommand=0;firstEnrollmentCommand=0;megaEnrollmentConfirmed=false;nextEnrollmentPoll=0;return;
  }
 }else{
  if(receipt["job"].isNull())return;
  const String job=receipt["job"].as<String>();if(job.length()!=36)return;
  for(size_t i=0;i<36;i++){char c=job[i];bool dash=i==8||i==13||i==18||i==23;if(dash?c!='-':!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return;}
  EnrollmentState next{};next.version=1;strlcpy(next.job,job.c_str(),sizeof(next.job));strlcpy(next.step,"waiting_for_mega",sizeof(next.step));
  if(!saveEnrollment(next))return;
  megaEnrollmentConfirmed=false;firstEnrollmentCommand=0;lastEnrollmentCommand=0;
 }
 if(!enrollment.result && !megaEnrollmentConfirmed && firstEnrollmentCommand && millis()-firstEnrollmentCommand>=30000){
  EnrollmentState next=enrollment;next.version=1;next.result=true;
  strlcpy(next.step,"failed",sizeof(next.step));strlcpy(next.error,"MEGA_NOT_RESPONDING",sizeof(next.error));
  if(!saveEnrollment(next))enrollment=next;
  Serial.println("ENROLLMENT FAILED: No Mega acknowledgement. Check enrollment firmware, ESP32 TX17 -> Mega RX19, Mega TX18 -> level shifter -> ESP32 RX16, and common GND.");
  nextEnrollmentPoll=0;return;
 }
 if(!enrollment.result && !megaEnrollmentConfirmed && (!lastEnrollmentCommand || millis()-lastEnrollmentCommand>=10000)){
  MegaSerial.print("ENROLL,");MegaSerial.println(enrollment.job);lastEnrollmentCommand=millis();
  if(!firstEnrollmentCommand)firstEnrollmentCommand=lastEnrollmentCommand;
  Serial.println("ENROLLMENT: command sent to Mega; waiting for acknowledgement on ESP32 RX16.");
 }
}
