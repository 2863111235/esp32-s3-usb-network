// SPDX-License-Identifier: MIT
// Wi-Fi STA -> lwIP NAPT -> USB CDC-NCM. Arduino-ESP32 3.3.12.
// USB stack: EspUsbDevice 2.6.1 (MIT). Routing design: MQM-M USB router
// and Espressif tusb_ncm examples (CC0-1.0 OR Unlicense).
#include <Arduino.h>
#include <EspUsbDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_netif.h"
#include "esp_mac.h"

static const IPAddress USB_IP(192,168,7,1), USB_MASK(255,255,255,0);
static const uint8_t USB_MAC[6] = {0x02,0xa3,0x53,0x19,0x00,0x01};
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "WiFi Adapter Console");
EspUsbDeviceNet net(device);
WebServer server(USB_IP,80);
Preferences preferences;
String savedSsid, pendingSsid, pendingPassword, message = "请选择一个 2.4 GHz Wi-Fi。";
String token, serialNumber;
bool connectPending = false, usbReady = false, naptEnabled = false;
bool scanRunning = false, scanRequested = false;
uint32_t connectStarted = 0;
IPAddress appliedIp;

static String jsonQuote(const String &value) {
  String out = "\"";
  for (size_t i=0; i<value.length(); ++i) {
    uint8_t c=value[i];
    if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
    else if (c<32) { char escape[7]; snprintf(escape,sizeof(escape),"\\u%04x",c); out+=escape; }
    else out+=char(c);
  }
  return out+'"';
}

static bool localClient() {
  return (uint32_t(server.client().remoteIP()) & uint32_t(USB_MASK)) ==
         (uint32_t(USB_IP) & uint32_t(USB_MASK));
}

static bool allowChange() {
  if (!localClient() || server.header("X-Device-Token")!=token) {
    server.send(403,"application/json","{\"error\":\"请从 USB 配置页重新操作。\"}");
    return false;
  }
  return true;
}

static void sendStatus() {
  String out="{\"connected\":"+String(WiFi.status()==WL_CONNECTED ? "true":"false");
  out+=",\"ssid\":"+jsonQuote(savedSsid);
  out+=",\"ip\":"+jsonQuote(WiFi.localIP().toString());
  out+=",\"dns\":"+jsonQuote(WiFi.dnsIP().toString());
  out+=",\"rssi\":"+String(WiFi.status()==WL_CONNECTED ? WiFi.RSSI():0);
  out+=",\"nat\":"+String(naptEnabled ? "true":"false");
  out+=",\"message\":"+jsonQuote(message)+"}";
  server.send(200,"application/json; charset=utf-8",out);
}

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>S3 Wi-Fi 网卡</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f4f0;color:#202722;font:16px/1.65 system-ui,sans-serif}
main{max-width:620px;margin:7vh auto;padding:28px}h1{font-size:30px;line-height:1.2;margin:12px 0}
.eyebrow{font-size:13px;letter-spacing:.1em;color:#4a5b4c}.panel{background:#fff;border:1px solid #d7dbd3;padding:24px;margin:24px 0;border-radius:12px}
label{display:block;margin:18px 0 6px;font-weight:600}input,select,button{font:inherit;min-height:46px;border-radius:6px}
input,select{width:100%;padding:9px 12px;border:1px solid #89968a;background:#fff;color:#202722}
button{padding:10px 18px;border:1px solid #284f3b;background:#284f3b;color:white;cursor:pointer}button.secondary{background:transparent;color:#284f3b}
button:disabled{opacity:.6;cursor:wait}:focus-visible{outline:3px solid #2179bb;outline-offset:3px}
.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.hint,small{color:#4b5b4e;font-size:14px}
#state{min-height:3em;white-space:pre-line}#error{color:#9b251e;min-height:1.6em}code{font-size:14px}
</style></head><body><main><div class="eyebrow">ESP32-S3 · USB NETWORK</div>
<h1>把 Wi-Fi 接到这台电脑</h1><p>选择附近的 2.4 GHz Wi-Fi。保存后，网卡会自动连接，重启也会记住设置。</p>
<section class="panel" aria-labelledby="connection"><h2 id="connection">连接状态</h2><p id="state" role="status">正在读取状态…</p></section>
<form id="wifi" class="panel" aria-describedby="helper"><div class="row"><h2 style="flex:1">Wi-Fi 设置</h2><button class="secondary" type="button" id="scan">扫描网络</button></div>
<label for="networks">附近的网络</label><select id="networks"><option value="">也可以在下方手动输入</option></select>
<label for="ssid">Wi-Fi 名称</label><input id="ssid" name="ssid" required autocomplete="off" aria-describedby="helper error">
<label for="password">Wi-Fi 密码</label><input id="password" name="password" type="password" autocomplete="new-password" aria-describedby="helper error">
<p id="helper" class="hint">隐藏网络可手动输入名称；开放网络留空密码。密码只保存在开发板中。</p>
<p id="error" role="alert"></p><button type="submit" id="save">保存并连接</button></form>
<p class="hint">只支持 2.4 GHz。连接成功后，如电脑尚未获得新 DNS，可重新插拔开发板刷新网络配置。此模式无需开启 Windows 网络共享。</p>
</main><script>
const token='__TOKEN__',state=document.querySelector('#state'),error=document.querySelector('#error');let busy=false;
async function api(path,options={}){const response=await fetch(path,{cache:'no-store',...options});const data=await response.json();if(!response.ok)throw Error(data.error||'操作失败');return data}
async function status(){try{const d=await api('/api/status');state.textContent=d.message+(d.connected?'\n'+d.ssid+' · '+d.ip+' · '+d.rssi+' dBm':'')}catch(e){state.textContent='暂时无法读取网卡，请检查 USB 连接。'}}
async function scan(){const button=document.querySelector('#scan');button.disabled=true;button.textContent='扫描中…';try{let d=await api('/api/scan');while(d.scanning){await new Promise(r=>setTimeout(r,800));d=await api('/api/scan')}const select=document.querySelector('#networks');select.replaceChildren(new Option('选择一个网络，或手动输入',''));for(const n of d.networks)select.add(new Option(n.ssid+' · '+n.rssi+' dBm'+(n.open?' · 开放':''),n.ssid))}catch(e){error.textContent=e.message}finally{button.disabled=false;button.textContent='扫描网络'}}
document.querySelector('#networks').onchange=e=>{if(e.target.value)document.querySelector('#ssid').value=e.target.value};document.querySelector('#scan').onclick=scan;
document.querySelector('#wifi').onsubmit=async e=>{e.preventDefault();if(busy)return;const ssid=document.querySelector('#ssid'),password=document.querySelector('#password'),save=document.querySelector('#save');const bytes=new TextEncoder();if(bytes.encode(ssid.value).length>32){error.textContent='Wi-Fi 名称不能超过 32 字节。';ssid.setAttribute('aria-invalid','true');ssid.focus();return}ssid.removeAttribute('aria-invalid');error.textContent='';busy=true;save.disabled=true;try{await api('/api/connect',{method:'POST',headers:{'X-Device-Token':token},body:new URLSearchParams({ssid:ssid.value,password:password.value})});password.value='';state.textContent='正在连接，请稍候…'}catch(e){error.textContent=e.message}finally{busy=false;save.disabled=false;status()}};
status();setInterval(status,2000);
</script></body></html>)HTML";

static void setupWeb() {
  const char *headers[]={"X-Device-Token"}; server.collectHeaders(headers,1);
  server.on("/",HTTP_GET,[]{
    if (!localClient()) { server.send(403,"text/plain","USB access only"); return; }
    String html=FPSTR(PAGE); html.replace("__TOKEN__",token);
    server.sendHeader("Cache-Control","no-store");
    server.sendHeader("Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
    server.send(200,"text/html; charset=utf-8",html);
  });
  server.on("/api/status",HTTP_GET,[]{if (localClient()) sendStatus(); else server.send(403);});
  server.on("/api/connect",HTTP_POST,[]{
    if (!allowChange()) return;
    String ssid=server.arg("ssid"),password=server.arg("password");
    if (ssid.isEmpty() || ssid.length()>32 || password.length()>63 || (!password.isEmpty() && password.length()<8)) {
      server.send(400,"application/json; charset=utf-8","{\"error\":\"请检查名称（1–32 字节）和密码（8–63 字节，开放网络留空）。\"}"); return;
    }
    pendingSsid=ssid; pendingPassword=password; connectPending=true;
    server.send(202,"application/json","{\"accepted\":true}");
  });
  server.on("/api/scan",HTTP_GET,[]{
    if (!localClient()) {server.send(403);return;}
    int count=WiFi.scanComplete();
    if (scanRunning && count>=0) scanRunning=false;
    if (!scanRequested || count==WIFI_SCAN_FAILED) {
      if (!scanRunning) {WiFi.scanNetworks(true,true);scanRunning=true;scanRequested=true;}
    }
    if (scanRunning || count<0) {server.send(200,"application/json","{\"scanning\":true}");return;}
    String out="{\"scanning\":false,\"networks\":[";
    unsigned added=0;
    for (int i=0;i<count && added<24;++i) {
      if (WiFi.SSID(i).isEmpty()) continue;
      if (added++) out+=',';
      out+="{\"ssid\":"+jsonQuote(WiFi.SSID(i))+",\"rssi\":"+String(WiFi.RSSI(i))+",\"open\":"+String(WiFi.encryptionType(i)==WIFI_AUTH_OPEN ? "true":"false")+"}";
    }
    server.send(200,"application/json; charset=utf-8",out+"]}");
    scanRequested=false;
  });
  server.onNotFound([]{server.send(404,"text/plain","Not found");});
  server.begin();
}

static void applyRouting() {
  esp_netif_t *usb=esp_netif_get_handle_from_ifkey("USB_NCM");
  esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!usb || !sta) {message="网络接口尚未准备好。";return;}
  const IPAddress mask=WiFi.subnetMask();
  if ((uint32_t(WiFi.localIP()) & uint32_t(mask))==(uint32_t(USB_IP) & uint32_t(mask))) {
    message="Wi-Fi 与 USB 网段冲突，请连接其他网络。";return;
  }
  esp_netif_set_default_netif(sta);
  esp_err_t result=esp_netif_napt_enable(usb);
  if (result!=ESP_OK) {message="NAT 启动失败："+String(esp_err_to_name(result));return;}
  naptEnabled=true;appliedIp=WiFi.localIP();
  // Advertise the actual uplink DNS for the next DHCP lease/renewal.
  esp_netif_dns_info_t dns={};
  if (esp_netif_get_dns_info(sta,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK && dns.ip.u_addr.ip4.addr) {
    esp_netif_dhcps_stop(usb);
    esp_netif_set_dns_info(usb,ESP_NETIF_DNS_MAIN,&dns);
    esp_netif_dhcps_start(usb);
  }
  message="已连接，USB 网卡可以上网。";
  console.printf("WIFI_ADAPTER connected=1 ssid=%s ip=%s dns=%s nat=1\n",savedSsid.c_str(),WiFi.localIP().toString().c_str(),WiFi.dnsIP().toString().c_str());
}

void setup() {
  uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);
  char id[13];snprintf(id,sizeof(id),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);serialNumber=id;
  char nonce[33];snprintf(nonce,sizeof(nonce),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());token=nonce;
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.setSleep(false);
  net.macAddress(USB_MAC);net.ipConfig(USB_IP,USB_IP,USB_MASK);net.dhcpServer(true);net.dhcpAdvertiseGateway(true);net.dhcpDns(IPAddress(223,5,5,5));
  EspUsbDeviceConfig config;config.vid=0x303a;config.pid=0x1001;config.deviceVersion=0x0300;config.manufacturer="Espressif Systems";config.product="ESP32-S3 WiFi USB Adapter";config.serialNumber=serialNumber.c_str();
  if (!device.begin(config) || !net.beginNetwork()) return;
  usbReady=true;
  preferences.begin("usb-wifi",false);savedSsid=preferences.getString("ssid","");
  if (!savedSsid.isEmpty()) {pendingSsid=savedSsid;pendingPassword=preferences.getString("pass","");connectPending=true;}
  setupWeb();
}

void loop() {
  if (!usbReady) {delay(1000);return;}
  server.handleClient();
  if (connectPending) {
    if (naptEnabled) {esp_netif_napt_disable(esp_netif_get_handle_from_ifkey("USB_NCM"));naptEnabled=false;}
    WiFi.scanDelete();scanRequested=false;scanRunning=false;
    WiFi.disconnect();savedSsid=pendingSsid;
    preferences.putString("ssid",pendingSsid);preferences.putString("pass",pendingPassword);
    WiFi.begin(pendingSsid.c_str(),pendingPassword.c_str());pendingPassword="";connectStarted=millis();connectPending=false;message="正在连接 Wi-Fi…";
  }
  if (WiFi.status()==WL_CONNECTED) {
    if (!naptEnabled || appliedIp!=WiFi.localIP()) applyRouting();
  } else {
    if (naptEnabled) {esp_netif_napt_disable(esp_netif_get_handle_from_ifkey("USB_NCM"));naptEnabled=false;message="Wi-Fi 已断开，正在自动重连…";}
    if (connectStarted && millis()-connectStarted>30000) {message="尚未连接成功，请检查密码、2.4 GHz 频段和信号。";connectStarted=0;}
  }
  static uint32_t lastStatus;
  if (console.available()) {
    char command=console.read();
    if (command=='b') device.rebootToBootloader();
    if (command=='r') ESP.restart();
    if (command=='s') lastStatus=0;
  }
  if (millis()-lastStatus>5000) {lastStatus=millis();console.printf("WIFI_ADAPTER usb=%d wifi=%d nat=%d ip=%s heap=%lu portal=http://192.168.7.1/\n",net.linkUp(),WiFi.status(),naptEnabled,WiFi.localIP().toString().c_str(),(unsigned long)ESP.getFreeHeap());}
  delay(2);
}
