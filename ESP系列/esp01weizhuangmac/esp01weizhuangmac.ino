#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <user_interface.h>

// ========== 默认配置 ==========
const char* DEFAULT_AP_SSID = "AlxaTax-2";
const char* DEFAULT_AP_PASSWORD = "alssw0483";
uint8_t DEFAULT_MAC[] = {0x06, 0x69, 0x6C, 0x45, 0xCA, 0x21};

// ========== Web服务器 ==========
ESP8266WebServer server(80);

// ========== EEPROM配置结构 ==========
#define EEPROM_SIZE 1024
struct Config {
  char ap_ssid[32];
  char ap_password[64];
  char sta_ssid[32];
  char sta_password[64];
  uint8_t mac[6];
  bool sta_configured;
  bool ap_configured;
};

Config config;
bool sta_connected = false;
unsigned long previousMillis = 0;
const long interval = 10000;

// ========== 函数声明 ==========
void setupHandlers();
void handleRoot();
void handleSave();
void handleReboot();
void handleScan();
void handleScanMAC();
void handleStatus();
void handleReset();
void loadConfig();
void saveConfig();
void setAPMAC();
void connectToWiFi();
void printMAC(uint8_t* mac);
String macToString(uint8_t* mac);
String bssidToString(uint8_t* bssid);
bool parseMAC(const String& macStr, uint8_t* mac);
void printNetworkInfo();
void checkWiFiStatus();
void restartAP();
String getEncryptionTypeStr(uint8_t encryptionType);
void sortNetworksByRSSI(int* indices, int n);

// ========== HTML页面 ==========
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP WiFi配置</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <style>
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body { font-family: Arial, "Microsoft YaHei", sans-serif; margin: 0; padding: 15px; background: #f5f5f5; font-size: 16px; touch-action: manipulation; }
    .container { max-width: 100%; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; margin-top: 0; }
    .form-group { margin-bottom: 20px; }
    label { display: block; margin-bottom: 8px; font-weight: bold; }
    input, select { width: 100%; padding: 12px 15px; border: 2px solid #ddd; border-radius: 8px; font-size: 16px; min-height: 48px; }
    input:focus, select:focus { border-color: #007cba; outline: none; }
    button { width: 100%; padding: 15px; background: #007cba; color: white; border: none; border-radius: 8px; cursor: pointer; font-size: 18px; font-weight: bold; min-height: 50px; margin-top: 10px; }
    button:hover { background: #005a87; }
    button:disabled { background: #cccccc; cursor: not-allowed; }
    .status { padding: 15px; border-radius: 8px; margin: 15px 0; font-size: 14px; }
    .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
    .info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
    .scan-btn { background: #28a745; margin-bottom: 15px; }
    .scan-btn:hover { background: #218838; }
    .tab { display: flex; border: 1px solid #ccc; background-color: #f1f1f1; border-radius: 8px 8px 0 0; }
    .tab button { flex: 1; border: none; padding: 14px 16px; font-size: 16px; background: inherit; cursor: pointer; }
    .tab button.active { background-color: #ccc; }
    .tabcontent { display: none; padding: 15px; border: 1px solid #ccc; border-top: none; border-radius: 0 0 8px 8px; }
    .mac-display { font-family: monospace; background: #f8f9fa; padding: 5px 10px; border-radius: 4px; border: 1px solid #dee2e6; }
    .link-bar { display: flex; justify-content: space-around; margin-top: 20px; flex-wrap: wrap; }
    .link-bar a { color: #007cba; text-decoration: none; padding: 10px 15px; border: 1px solid #007cba; border-radius: 6px; margin: 5px; flex: 1; text-align: center; min-width: 100px; }
    .link-bar a:hover { background: #007cba; color: white; }
    .wifi-mac-list { max-height: 200px; overflow-y: auto; border: 1px solid #ddd; border-radius: 5px; padding: 10px; margin-top: 10px; background: #f9f9f9; }
    .wifi-mac-item { padding: 8px; border-bottom: 1px solid #eee; }
    .wifi-mac-item:hover { background: #e9ecef; }
    .use-mac-btn { background: #17a2b8; padding: 8px 12px; font-size: 14px; min-height: auto; width: auto; margin: 5px 0; }
    .reset-btn { background: #dc3545; }
    .reset-btn:hover { background: #c82333; }
    .loading { display: none; text-align: center; padding: 20px; }
    .signal-strength { display: inline-block; width: 60px; text-align: right; }
    .signal-strong { color: #28a745; }
    .signal-medium { color: #ffc107; }
    .signal-weak { color: #dc3545; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP WiFi配置</h1>
    
    <div class="status info">
      <strong>系统状态:</strong><br>
      AP热点: %AP_SSID%<br>
      AP IP: %AP_IP%<br>
      AP MAC: <span class="mac-display">%AP_MAC%</span><br>
      STA连接: %STA_STATUS%<br>
      %STA_DETAILS%
    </div>

    <div class="tab">
      <button class="tablinks active" onclick="openTab(event, 'APTab')">AP设置</button>
      <button class="tablinks" onclick="openTab(event, 'WiFiTab')">WiFi连接</button>
      <button class="tablinks" onclick="openTab(event, 'MacTab')">MAC地址</button>
    </div>

    <div id="APTab" class="tabcontent" style="display:block;">
      <form action="/save" method="POST" onsubmit="return validateForm(this)">
        <input type="hidden" name="type" value="ap">
        <div class="form-group">
          <label for="ap_ssid">AP热点名称:</label>
          <input type="text" id="ap_ssid" name="ap_ssid" value="%AP_SSID%" placeholder="输入AP热点名称" required minlength="1" maxlength="31">
        </div>
        <div class="form-group">
          <label for="ap_password">AP热点密码:</label>
          <input type="password" id="ap_password" name="ap_password" value="%AP_PASSWORD%" placeholder="输入AP热点密码" minlength="8" maxlength="63">
        </div>
        <button type="submit">保存AP设置</button>
      </form>
    </div>

    <div id="WiFiTab" class="tabcontent">
      <button type="button" class="scan-btn" onclick="scanWiFi()">扫描可用WiFi</button>
      <form action="/save" method="POST" onsubmit="return validateForm(this)">
        <input type="hidden" name="type" value="wifi">
        <div class="form-group">
          <label for="sta_ssid">WiFi名称:</label>
          <input type="text" id="sta_ssid" name="sta_ssid" value="%STA_SSID%" placeholder="输入WiFi名称" required minlength="1" maxlength="31">
        </div>
        <div class="form-group">
          <label for="sta_password">WiFi密码:</label>
          <input type="password" id="sta_password" name="sta_password" value="%STA_PASSWORD%" placeholder="输入WiFi密码" minlength="8" maxlength="63">
        </div>
        <button type="submit">保存并连接</button>
      </form>
    </div>

    <div id="MacTab" class="tabcontent">
      <div class="status info">
        <strong>MAC地址信息:</strong><br>
        当前MAC: <span class="mac-display">%CURRENT_MAC%</span><br>
        默认MAC: <span class="mac-display">06:69:6C:45:CA:21</span>
      </div>
      
      <form action="/save" method="POST" onsubmit="return validateMAC(this)">
        <input type="hidden" name="type" value="mac">
        <div class="form-group">
          <label for="mac_address">自定义MAC地址:</label>
          <input type="text" id="mac_address" name="mac_address" value="%CURRENT_MAC%" placeholder="06:69:6C:45:CA:21" pattern="([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}">
        </div>
        <button type="submit">保存MAC地址</button>
      </form>
      
      <button type="button" class="scan-btn" onclick="scanWiFiMAC()">扫描WiFi MAC地址</button>
      <div id="wifiMacList" class="wifi-mac-list" style="display:none;">
        <h4>扫描到的WiFi MAC地址:</h4>
        <div id="wifiMacItems"></div>
      </div>
    </div>

    <div class="loading" id="loading">
      <p>扫描中，请稍候...</p>
    </div>

    <div class="link-bar">
      <a href="/status">系统状态</a>
      <a href="/reset" onclick="return confirm('确定要恢复默认设置吗？这将清除所有配置！')">恢复默认</a>
      <a href="/reboot">重启设备</a>
    </div>
  </div>

  <script>
    function openTab(evt, tabName) {
      var i, tabcontent = document.getElementsByClassName("tabcontent");
      for (i = 0; i < tabcontent.length; i++) tabcontent[i].style.display = "none";
      var tablinks = document.getElementsByClassName("tablinks");
      for (i = 0; i < tablinks.length; i++) tablinks[i].className = tablinks[i].className.replace(" active", "");
      document.getElementById(tabName).style.display = "block";
      if (evt) evt.currentTarget.className += " active";
    }

    function validateForm(form) {
      var inputs = form.querySelectorAll('input[required]');
      for (var i = 0; i < inputs.length; i++) {
        if (!inputs[i].value.trim()) {
          alert('请填写所有必填字段');
          inputs[i].focus();
          return false;
        }
      }
      return true;
    }

    function validateMAC(form) {
      var macInput = document.getElementById('mac_address');
      var macPattern = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/;
      if (macInput.value && !macPattern.test(macInput.value)) {
        alert('MAC地址格式不正确，请使用格式: 06:69:6C:45:CA:21');
        macInput.focus();
        return false;
      }
      return true;
    }

    function scanWiFi() {
      window.open('/scan', 'wifiScan', 'width=600,height=700,scrollbars=yes');
    }

    function scanWiFiMAC() {
      var btn = document.querySelector('#MacTab .scan-btn');
      var wifiMacList = document.getElementById('wifiMacList');
      var wifiMacItems = document.getElementById('wifiMacItems');
      var loading = document.getElementById('loading');
      
      btn.innerHTML = '扫描中...';
      btn.disabled = true;
      loading.style.display = 'block';
      wifiMacItems.innerHTML = '';
      wifiMacList.style.display = 'block';
      
      fetch('/scanmac')
        .then(response => {
          if (!response.ok) throw new Error('网络响应不正常');
          return response.text();
        })
        .then(html => {
          wifiMacItems.innerHTML = html;
          btn.innerHTML = '扫描WiFi MAC地址';
          btn.disabled = false;
          loading.style.display = 'none';
        })
        .catch(error => {
          wifiMacItems.innerHTML = '<p style="color:red;">扫描失败: ' + error.message + '</p>';
          btn.innerHTML = '扫描WiFi MAC地址';
          btn.disabled = false;
          loading.style.display = 'none';
        });
    }

    function useMacAddress(mac) {
      document.getElementById('mac_address').value = mac;
    }

    function selectSSID(ssid) {
      window.opener.document.getElementById('sta_ssid').value = ssid;
      window.close();
    }

    // 自动检查WiFi状态
    setInterval(function() {
      fetch('/status')
        .then(response => response.text())
        .then(html => {
          // 这里可以更新状态显示，但需要更复杂的解析
          console.log('状态已更新');
        })
        .catch(error => console.log('状态检查失败:', error));
    }, 30000);
  </script>
</body>
</html>
)rawliteral";

// ========== 函数实现 ==========

void loadConfig() {
  EEPROM.get(0, config);
  
  // 检查配置有效性
  if (config.ap_ssid[0] == 0 || !config.ap_configured) {
    strcpy(config.ap_ssid, DEFAULT_AP_SSID);
    strcpy(config.ap_password, DEFAULT_AP_PASSWORD);
    memcpy(config.mac, DEFAULT_MAC, 6);
    config.ap_configured = true;
    saveConfig();
  }
  
  Serial.print("AP SSID: "); Serial.println(config.ap_ssid);
  Serial.print("STA配置: "); Serial.println(config.sta_configured ? config.sta_ssid : "未配置");
  Serial.print("MAC地址: "); printMAC(config.mac);
}

void saveConfig() {
  EEPROM.put(0, config);
  bool success = EEPROM.commit();
  Serial.println(success ? "配置已保存" : "配置保存失败");
}

void resetConfig() {
  // 重置所有配置为默认值
  strcpy(config.ap_ssid, DEFAULT_AP_SSID);
  strcpy(config.ap_password, DEFAULT_AP_PASSWORD);
  memset(config.sta_ssid, 0, sizeof(config.sta_ssid));
  memset(config.sta_password, 0, sizeof(config.sta_password));
  memcpy(config.mac, DEFAULT_MAC, 6);
  config.sta_configured = false;
  config.ap_configured = true;
  
  saveConfig();
  Serial.println("已恢复默认设置");
}

void setAPMAC() {
  bool macSet = wifi_set_macaddr(SOFTAP_IF, config.mac);
  Serial.print(macSet ? "AP MAC设置成功: " : "AP MAC设置失败: ");
  printMAC(config.mac);
  delay(500);
}

void restartAP() {
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.softAP(config.ap_ssid, config.ap_password);
  Serial.println("AP热点已重启");
}

void connectToWiFi() {
  if (!config.sta_configured || strlen(config.sta_ssid) == 0) return;
  
  Serial.print("连接到WiFi: "); Serial.println(config.sta_ssid);
  WiFi.begin(config.sta_ssid, config.sta_password);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    sta_connected = true;
    Serial.println("连接成功");
    Serial.print("IP地址: "); Serial.println(WiFi.localIP());
  } else {
    sta_connected = false;
    Serial.println("连接失败");
    WiFi.disconnect();
  }
}

void printMAC(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(macStr);
}

String macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

String bssidToString(uint8_t* bssid) {
  char bssidStr[18];
  snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  return String(bssidStr);
}

bool parseMAC(const String& macStr, uint8_t* mac) {
  String cleanMac = macStr;
  cleanMac.replace(":", "");
  cleanMac.replace(" ", "");
  cleanMac.replace("-", "");
  cleanMac.toUpperCase();
  
  if (cleanMac.length() != 12) return false;
  
  for (int i = 0; i < 12; i++) {
    char c = cleanMac[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
  }
  
  for (int i = 0; i < 6; i++) {
    String byteStr = cleanMac.substring(i * 2, i * 2 + 2);
    mac[i] = strtol(byteStr.c_str(), NULL, 16);
  }
  
  return true;
}

String getEncryptionTypeStr(uint8_t encryptionType) {
  switch(encryptionType) {
    case ENC_TYPE_NONE: return "开放网络";
    case ENC_TYPE_WEP: return "WEP";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_AUTO: return "自动";
    default: return "未知";
  }
}

void sortNetworksByRSSI(int* indices, int n) {
  // 简单的冒泡排序按信号强度排序
  for (int i = 0; i < n-1; i++) {
    for (int j = i+1; j < n; j++) {
      if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
      }
    }
  }
}

String getSignalStrengthClass(int rssi) {
  if (rssi >= -50) return "signal-strong";
  else if (rssi >= -70) return "signal-medium";
  else return "signal-weak";
}

void printNetworkInfo() {
  Serial.println("\n=== 网络信息 ===");
  Serial.print("AP热点: "); Serial.println(config.ap_ssid);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  Serial.print("AP MAC: "); Serial.println(WiFi.softAPmacAddress());
  
  if (sta_connected) {
    Serial.print("STA连接: "); Serial.println(config.sta_ssid);
    Serial.print("STA IP: "); Serial.println(WiFi.localIP());
    Serial.print("信号强度: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  } else {
    Serial.print("STA连接: "); 
    Serial.println(config.sta_configured ? "未连接" : "未配置");
  }
  Serial.println("================");
}

void checkWiFiStatus() {
  if (WiFi.status() != WL_CONNECTED && sta_connected) {
    Serial.println("WiFi连接断开");
    sta_connected = false;
  } else if (WiFi.status() == WL_CONNECTED && !sta_connected) {
    Serial.println("WiFi连接恢复");
    sta_connected = true;
  }
}

// ========== HTTP请求处理 ==========

void handleRoot() {
  String page = htmlPage;
  page.replace("%AP_SSID%", config.ap_ssid);
  page.replace("%AP_PASSWORD%", config.ap_password);
  page.replace("%AP_IP%", WiFi.softAPIP().toString());
  page.replace("%AP_MAC%", WiFi.softAPmacAddress());
  page.replace("%STA_SSID%", config.sta_ssid);
  page.replace("%STA_PASSWORD%", config.sta_password);
  page.replace("%CURRENT_MAC%", macToString(config.mac));
  
  if (sta_connected) {
    page.replace("%STA_STATUS%", "已连接");
    String staDetails = "连接网络: " + String(config.sta_ssid) + "<br>" +
                       "STA IP: " + WiFi.localIP().toString() + "<br>" +
                       "信号强度: <span class='" + getSignalStrengthClass(WiFi.RSSI()) + "'>" + String(WiFi.RSSI()) + " dBm</span>";
    page.replace("%STA_DETAILS%", staDetails);
  } else if (config.sta_configured) {
    page.replace("%STA_STATUS%", "连接失败");
    page.replace("%STA_DETAILS%", "保存的网络: " + String(config.sta_ssid));
  } else {
    page.replace("%STA_STATUS%", "未配置");
    page.replace("%STA_DETAILS%", "");
  }
  
  server.send(200, "text/html; charset=utf-8", page);
}

void handleSave() {
  String type = server.arg("type");
  
  if (type == "ap") {
    String ap_ssid = server.arg("ap_ssid");
    String ap_password = server.arg("ap_password");
    
    if (ap_ssid.length() > 0 && ap_ssid.length() <= 31) {
      ap_ssid.toCharArray(config.ap_ssid, sizeof(config.ap_ssid));
      ap_password.toCharArray(config.ap_password, sizeof(config.ap_password));
      saveConfig();
      restartAP();
      server.send(200, "text/html; charset=utf-8", "<script>alert('AP设置已保存，热点已重启');window.location='/';</script>");
    } else {
      server.send(200, "text/html; charset=utf-8", "<script>alert('AP名称长度必须在1-31个字符之间');window.location='/';</script>");
    }
  }
  else if (type == "wifi") {
    String sta_ssid = server.arg("sta_ssid");
    String sta_password = server.arg("sta_password");
    
    if (sta_ssid.length() > 0 && sta_ssid.length() <= 31) {
      sta_ssid.toCharArray(config.sta_ssid, sizeof(config.sta_ssid));
      sta_password.toCharArray(config.sta_password, sizeof(config.sta_password));
      config.sta_configured = true;
      saveConfig();
      connectToWiFi();
      server.send(200, "text/html; charset=utf-8", "<script>alert('WiFi设置已保存');window.location='/';</script>");
    } else {
      server.send(200, "text/html; charset=utf-8", "<script>alert('WiFi名称长度必须在1-31个字符之间');window.location='/';</script>");
    }
  }
  else if (type == "mac") {
    String macStr = server.arg("mac_address");
    macStr.trim();
    
    if (macStr.length() == 0) {
      memcpy(config.mac, DEFAULT_MAC, 6);
      saveConfig();
      setAPMAC();
      server.send(200, "text/html; charset=utf-8", "<script>alert('已恢复默认MAC地址');window.location='/';</script>");
    } else if (parseMAC(macStr, config.mac)) {
      saveConfig();
      setAPMAC();
      server.send(200, "text/html; charset=utf-8", "<script>alert('MAC地址已保存');window.location='/';</script>");
    } else {
      server.send(200, "text/html; charset=utf-8", "<script>alert('MAC地址格式错误，请使用格式: 06:69:6C:45:CA:21');window.location='/';</script>");
    }
  }
}

void handleScan() {
  String page = "<!DOCTYPE html><html><head><title>WiFi扫描</title><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<style>body{font-family:Arial,'Microsoft YaHei',sans-serif;margin:0;padding:20px;background:#f5f5f5;}";
  page += ".container{max-width:100%;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  page += ".network-item{padding:10px;border-bottom:1px solid #eee;} .select-btn{background:#007cba;color:white;padding:5px 10px;text-decoration:none;border-radius:3px;}";
  page += ".signal-strong{color:#28a745;} .signal-medium{color:#ffc107;} .signal-weak{color:#dc3545;}</style></head><body>";
  page += "<div class='container'><h1>可用WiFi网络</h1>";
  
  int n = WiFi.scanNetworks();
  if (n == 0) {
    page += "<p>未找到WiFi网络</p>";
  } else {
    // 创建索引数组并排序
    int indices[n];
    for (int i = 0; i < n; i++) indices[i] = i;
    sortNetworksByRSSI(indices, n);
    
    page += "<p>找到 " + String(n) + " 个网络:</p>";
    for (int i = 0; i < n; i++) {
      int idx = indices[i];
      String ssid = WiFi.SSID(idx);
      if (ssid.length() == 0) ssid = "(隐藏网络)";
      
      page += "<div class='network-item'>";
      page += "<strong>" + ssid + "</strong><br>";
      page += "信号: <span class='" + getSignalStrengthClass(WiFi.RSSI(idx)) + "'>" + String(WiFi.RSSI(idx)) + " dBm</span> | ";
      page += getEncryptionTypeStr(WiFi.encryptionType(idx)) + "<br>";
      page += "MAC: <span style='font-family:monospace;background:#f8f9fa;padding:2px 5px;border-radius:3px;'>" + bssidToString(WiFi.BSSID(idx)) + "</span><br>";
      page += "<a href='javascript:void(0)' onclick=\"selectSSID('" + ssid + "')\" class='select-btn'>选择</a>";
      page += "</div>";
    }
  }
  
  page += "<br><a href='javascript:window.close()' style='background:#6c757d;color:white;padding:10px 15px;text-decoration:none;border-radius:5px;'>关闭</a>";
  page += "</div><script>function selectSSID(ssid){window.opener.document.getElementById('sta_ssid').value=ssid;window.close();}</script>";
  page += "</body></html>";
  
  server.send(200, "text/html; charset=utf-8", page);
}

void handleScanMAC() {
  String page = "";
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    page += "<p>未找到WiFi网络</p>";
  } else {
    // 创建索引数组并排序
    int indices[n];
    for (int i = 0; i < n; i++) indices[i] = i;
    sortNetworksByRSSI(indices, n);
    
    for (int i = 0; i < n; i++) {
      int idx = indices[i];
      String ssid = WiFi.SSID(idx);
      if (ssid.length() == 0) ssid = "(隐藏网络)";
      
      page += "<div class='wifi-mac-item'>";
      page += "<strong>" + ssid + "</strong><br>";
      page += "信号: <span class='" + getSignalStrengthClass(WiFi.RSSI(idx)) + "'>" + String(WiFi.RSSI(idx)) + " dBm</span> | ";
      page += getEncryptionTypeStr(WiFi.encryptionType(idx)) + "<br>";
      page += "MAC: <span class='mac-display'>" + bssidToString(WiFi.BSSID(idx)) + "</span><br>";
      page += "<button class='use-mac-btn' onclick='useMacAddress(\"" + bssidToString(WiFi.BSSID(idx)) + "\")'>使用此MAC</button>";
      page += "</div>";
    }
  }
  
  server.send(200, "text/html; charset=utf-8", page);
}

void handleStatus() {
  String page = "<!DOCTYPE html><html><head><title>系统状态</title><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body>";
  page += "<div style='max-width:100%;margin:0 auto;padding:20px;'><h1>系统状态</h1>";
  
  page += "<h2>AP热点信息</h2>";
  page += "<p><strong>SSID:</strong> " + String(config.ap_ssid) + "</p>";
  page += "<p><strong>IP地址:</strong> " + WiFi.softAPIP().toString() + "</p>";
  page += "<p><strong>MAC地址:</strong> <span style='font-family:monospace;background:#f8f9fa;padding:2px 5px;border-radius:3px;'>" + WiFi.softAPmacAddress() + "</span></p>";
  page += "<p><strong>连接设备:</strong> " + String(WiFi.softAPgetStationNum()) + "</p>";
  
  page += "<h2>WiFi连接信息</h2>";
  if (sta_connected) {
    page += "<p style='color:green;'><strong>状态:</strong> 已连接</p>";
    page += "<p><strong>SSID:</strong> " + String(config.sta_ssid) + "</p>";
    page += "<p><strong>IP地址:</strong> " + WiFi.localIP().toString() + "</p>";
    page += "<p><strong>信号强度:</strong> <span class='" + getSignalStrengthClass(WiFi.RSSI()) + "'>" + String(WiFi.RSSI()) + " dBm</span></p>";
  } else {
    String statusText = config.sta_configured ? "连接失败" : "未配置";
    String color = config.sta_configured ? "red" : "orange";
    page += "<p style='color:" + color + ";'><strong>状态:</strong> " + statusText + "</p>";
    if (config.sta_configured) {
      page += "<p><strong>保存的网络:</strong> " + String(config.sta_ssid) + "</p>";
    }
  }
  
  page += "<h2>MAC地址信息</h2>";
  page += "<p><strong>当前MAC:</strong> <span style='font-family:monospace;background:#f8f9fa;padding:2px 5px;border-radius:3px;'>" + macToString(config.mac) + "</span></p>";
  page += "<p><strong>默认MAC:</strong> <span style='font-family:monospace;background:#f8f9fa;padding:2px 5px;border-radius:3px;'>06:69:6C:45:CA:21</span></p>";
  
  page += "<br><a href='/' style='background:#007cba;color:white;padding:10px 15px;text-decoration:none;border-radius:5px;'>返回</a>";
  page += "</div></body></html>";
  
  server.send(200, "text/html; charset=utf-8", page);
}

void handleReset() {
  resetConfig();
  server.send(200, "text/html; charset=utf-8", "<script>alert('已恢复默认设置');window.location='/';</script>");
}

void handleReboot() {
  server.send(200, "text/html; charset=utf-8", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><script>setTimeout(function(){window.location='/';},5000);</script></head><body style='text-align:center;padding:50px;'><h1>设备重启中...</h1><p>请等待设备重新启动</p></body></html>");
  delay(1000);
  ESP.restart();
}

void setupHandlers() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", handleScan);
  server.on("/scanmac", handleScanMAC);
  server.on("/status", handleStatus);
  server.on("/reset", handleReset);
  server.on("/reboot", handleReboot);
}

// ========== 主程序 ==========

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nESP-01S 智能配置系统启动");
  Serial.println("==============================");

  // 初始化EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();
  
  // 设置WiFi模式
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  
  // 设置AP MAC地址
  setAPMAC();
  
  // 配置并启动AP
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(config.ap_ssid, config.ap_password);

  // 连接WiFi
  connectToWiFi();

  // 启动Web服务器
  setupHandlers();
  server.begin();
  
  Serial.println("Web服务器启动完成");
  Serial.print("配置页面: http://");
  Serial.println(WiFi.softAPIP());
  
  printNetworkInfo();
}

void loop() {
  server.handleClient();
  
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    if (config.sta_configured) {
      checkWiFiStatus();
    }
    
    // 输出状态信息
    Serial.print("AP设备: "); Serial.print(WiFi.softAPgetStationNum());
    Serial.print(" | STA: ");
    if (sta_connected) {
      Serial.print("已连接 ("); Serial.print(config.sta_ssid); Serial.print(")");
      Serial.print(" | 信号: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    } else {
      Serial.println(config.sta_configured ? "连接失败" : "未配置");
    }
  }
  
  delay(100);
}