# MÔ HÌNH GIÁM SÁT VÀ CẢNH BÁO NGƯỜI CHẠY (RUNNER) SỬ DỤNG ESP32, FREERTOS VÀ MQTT

## 1. Giới thiệu
Dự án thiết kế mạch giám sát và cảnh báo cho người chạy bộ (runner) sử dụng ESP32 và cảm biến BMP180. BMP180 đo nhiệt độ và áp suất khí quyển, từ đó ESP32 tính độ cao so với mực nước biển. Dữ liệu được hiển thị trên Serial Monitor, gửi qua WiFi tới MQTT Broker để hiển thị trên Web/App, đồng thời nhận lệnh điều khiển LED và còi (loa) cảnh báo ở chế độ Manual. FreeRTOS tổ chức các chức năng thành các Task độc lập chạy trên hai lõi của ESP32.

## 2. Thành phần
- ESP32 DevKit
- Cảm biến BMP180 (nhiệt độ, áp suất khí quyển, giao tiếp I2C)
- 1 LED và 1 điện trở 220 Ω (đèn cảnh báo)
- 1 Buzzer/loa cảnh báo
- Wokwi, MQTT Broker (ví dụ HiveMQ hoặc Mosquitto), trình duyệt Web/App

## 3. Kết nối phần cứng (mô phỏng)

| Thiết bị | Kết nối ESP32 |
|---|---|
| BMP180 SDA | GPIO21 (SDA) |
| BMP180 SCL | GPIO22 (SCL) |
| BMP180 VCC | 3V3 |
| LED cảnh báo | GPIO18 (qua điện trở 220 Ω) |
| Buzzer (loa) | GPIO19 |
| GND | GND chung |

**Chú ý:** BMP180 chỉ hoạt động ở mức 3,3 V (tối đa khoảng 3,6 V), không cấp 5 V cho cảm biến hay chân I2C. Địa chỉ I2C của BMP180 là 0x77; module thực thường đã có sẵn điện trở kéo lên cho SDA/SCL. Khi dùng loa công suất lớn hoặc còi thực tế, cần mạch công suất (transistor/MOSFET) và nguồn phù hợp, không cấp dòng trực tiếp từ chân GPIO.

## 4. Phân chia Task FreeRTOS

| Task | Core | Chức năng |
|---|---|---|
| Task truyền data | Core 0 | Kết nối WiFi; nếu thành công thì kết nối MQTT Broker, đọc dữ liệu từ data Queue và gửi lên Broker; duy trì kết nối và nhận lệnh điều khiển (LED, loa, chế độ Auto/Manual) |
| Task đọc data | Core 1 | Đọc BMP180 (nhiệt độ, áp suất), tính độ cao; in ra Serial; gửi dữ liệu đầy đủ (nhiệt độ, áp suất, độ cao) vào data Queue và dữ liệu cảnh báo (nhiệt độ, độ cao) vào data Alert |
| Task cảnh báo | Core 1 | Nhận dữ liệu từ data Alert, so sánh ngưỡng và phát hiện thay đổi độ cao đột ngột; điều khiển LED/loa ở chế độ tự động |

## 5. Nguyên lý hoạt động
1. ESP32 đọc nhiệt độ và áp suất từ BMP180 theo chu kỳ (ví dụ 1–2 giây). Độ cao được tính từ áp suất theo công thức khí áp, với áp suất chuẩn mực nước biển p0 = 1013,25 hPa:

   `h = 44330 × [1 − (p / p0)^(1/5,255)]` (m)
2. Dữ liệu được hiển thị trên Serial Monitor, đồng thời gửi vào hai hàng đợi FreeRTOS: data Queue (nhiệt độ, áp suất, độ cao) và data Alert (nhiệt độ, độ cao).
3. Task truyền data lấy thông tin từ data Queue và gửi lên MQTT Broker qua WiFi (ví dụ các topic `runner/temperature`, `runner/pressure`, `runner/altitude`).
4. Web/App nhận dữ liệu MQTT và hiển thị số đo nhiệt độ, áp suất, độ cao (có thể bổ sung biểu đồ thời gian thực).
5. Ở chế độ tự động, Task cảnh báo bật LED và loa trong các trường hợp:
   - Nhiệt độ môi trường quanh người chạy vượt ngưỡng trên (ví dụ > 35 °C, nguy cơ sốc nhiệt) hoặc xuống dưới ngưỡng dưới (ví dụ < 10 °C, nguy cơ hạ thân nhiệt).
   - Độ cao thay đổi đột ngột: chênh lệch giữa hai lần đọc liên tiếp vượt ngưỡng (ví dụ > 5 m trong 2 giây), có thể do người chạy ngã hoặc di chuyển bất thường. Nên lọc nhiễu (trung bình trượt vài mẫu) để tránh báo giả.
6. Ở chế độ Manual, người dùng dùng hai nút trên Web/App để bật/tắt LED và loa (ví dụ gửi lệnh tới topic `runner/cmd/led` và `runner/cmd/buzzer`). Quy tắc ưu tiên: khi nhận lệnh Manual, hệ thống chuyển sang chế độ Manual và Task cảnh báo không ghi đè trạng thái LED/loa; khi người dùng chuyển lại chế độ Auto (topic `runner/mode`), việc điều khiển do Task cảnh báo đảm nhận.

## 6. Giao diện Web
- Ba thẻ HTML sử dụng class để hiển thị nhiệt độ, áp suất khí quyển và độ cao (so với mực nước biển) dạng số.
- Hai biểu đồ cập nhật theo thời gian thực: nhiệt độ và độ cao (tùy chọn).
- Hai nút điều khiển trạng thái LED và loa cảnh báo ở chế độ Manual (có thể thêm nút chuyển Auto/Manual).
- Web cần kết nối MQTT Broker qua MQTT over WebSocket (thư viện MQTT.js hoặc Paho) nếu chạy trong trình duyệt; ESP32 dùng cổng MQTT TCP (1883) còn Web dùng cổng WebSocket của Broker.

## 7. Kết quả dự kiến và kết luận
Mô hình được thiết kế để giám sát nhiệt độ, áp suất khí quyển và độ cao của người chạy, truyền dữ liệu qua MQTT và hiển thị trên Web/App, đồng thời cảnh báo bằng LED và loa khi nhiệt độ vượt ngưỡng hoặc độ cao thay đổi đột ngột, hay theo lệnh Manual. Việc tổ chức ba Task FreeRTOS giúp tách biệt quá trình thu thập, truyền dữ liệu và cảnh báo, giúp hệ thống hoạt động ổn định, không bị nghẽn khi mất WiFi. Cần chạy mô phỏng trên Wokwi và lưu hình minh chứng (sơ đồ mạch, Serial Monitor, giao diện Web) để xác nhận kết quả thực tế.
