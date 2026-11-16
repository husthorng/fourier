//https://gemini.google.com/app/bf585b2a9aa80c0b
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "arduinoFFT.h"

// ================= Wi-Fi 與 Google Drive 設定 =================
const char* WIFI_SSID     = "husthung";
const char* WIFI_PASSWORD = "ma2553968";
const String FILE_ID      = "1Kv1LeTLJ8w8C1rrw35ILeWSf-seuNjZr"; 

// Google Drive 直接下載網址
const String DOWNLOAD_URL = "https://drive.google.com/uc?export=download&id=" + FILE_ID;

// ================= FFT 參數設定 =================
const uint16_t SAMPLES = 1024;             // 必須為 2 的次方 (256, 512, 1024, 2048)
const double SAMPLING_FREQUENCY = 1000.0;  // 取樣頻率 (Hz)，需與原始 CSV 錄製設定一致

double vReal[SAMPLES];
double vImag[SAMPLES];

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);

// 從 Google Drive 串流讀取 CSV 並解析至 vReal
bool fetchCSVFromGoogleDrive() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  // 建議改用新版 Google Drive 直接下載端點，減少重新導向次數
  String directUrl = "https://drive.usercontent.google.com/download?id=" + FILE_ID + "&export=download&confirm=t";

  Serial.println("正在連線至 Google Drive 下載 CSV...");
  if (!http.begin(client, directUrl)) {
    Serial.println("HTTP 初始化失敗");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("下載失敗，HTTP 狀態碼: %d\n", httpCode);
    http.end();
    return false;
  }

  // 一次讀取完整回應字串，避免 SSL 串流分段導致 available() == 0 提早跳出
  String payload = http.getString();
  http.end();

  Serial.printf("成功接收資料，總長度: %d bytes\n", payload.length());
  // 印出前 150 個字元除錯，確認抓到的是 CSV 數字而非網頁 HTML
  Serial.println("--- 檔案開頭預覽 ---");
  Serial.println(payload.substring(0, 150));
  Serial.println("--------------------");

  if (payload.indexOf("<html") != -1 || payload.indexOf("<!DOCTYPE") != -1) {
    Serial.println("錯誤：抓到 HTML 網頁！請檢查 Google Drive 檔案權限是否設為「知道連結的任何人」。");
    return false;
  }

  // 逐行解析字串中的 CSV 數據
  uint16_t index = 0;
  int startPos = 0;

  while (startPos < payload.length() && index < SAMPLES) {
    int endPos = payload.indexOf('\n', startPos);
    if (endPos == -1) {
      endPos = payload.length();
    }

    String line = payload.substring(startPos, endPos);
    startPos = endPos + 1;
    line.trim();

    if (line.length() == 0) continue;

    // 跳過標題列 (若首字元不是數字、負號或小數點)
    if (index == 0 && !isDigit(line[0]) && line[0] != '-' && line[0] != '.') {
      continue;
    }

    // 擷取最後一欄數值
    int commaIndex = line.lastIndexOf(',');
    if (commaIndex != -1) {
      line = line.substring(commaIndex + 1);
      line.trim();
    }

    vReal[index] = line.toDouble();
    vImag[index] = 0.0;
    index++;
  }

  if (index == 0) {
    Serial.println("未解析到任何有效數據！請對照上方「檔案開頭預覽」檢查格式。");
    return false;
  }

  // 資料不足 SAMPLES 補 0
  for (uint16_t i = index; i < SAMPLES; i++) {
    vReal[i] = 0.0;
    vImag[i] = 0.0;
  }

  Serial.printf("成功解析 %d 筆振動數據！\n", index);
  return true;
}

void runFFTAnalysis() {
  FFT.dcRemoval();                                              // 去除直流偏移
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);     // 套用 Hamming 窗函數
  FFT.compute(FFTDirection::Forward);                           // 執行 FFT
  FFT.complexToMagnitude();                                     // 計算振幅

  double peakFrequency = FFT.majorPeak();
  Serial.println("=========================================");
  Serial.printf("主要振動頻率 (Peak Frequency): %.2f Hz\n", peakFrequency);
  Serial.println("=========================================");

  Serial.println("頻率(Hz)\t振幅(Magnitude)");
  for (uint16_t i = 1; i < (SAMPLES / 2); i++) {
    double freq = (i * 1.0 * SAMPLING_FREQUENCY) / SAMPLES;
    if (vReal[i] > 1.0) { // 過濾微弱底噪，可依訊號大小調整門檻
      Serial.printf("%.2f Hz\t\t%.4f\n", freq, vReal[i]);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. 連線 Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("正在連線 Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi 已連線！IP 位址: " + WiFi.localIP().toString());

  // 2. 下載 CSV 並執行 FFT
  if (fetchCSVFromGoogleDrive()) {
    runFFTAnalysis();
  }
}

void loop() {
  // 若需要定時重新抓取雲端最新 CSV，可在此加入 millis() 計時器呼叫 fetchCSVFromGoogleDrive()
}