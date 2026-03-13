# Proyek ESP8266 NodeMCU

Repositori ini berisi kode sumber untuk proyek berbasis mikrokontroler ESP8266. Program ini dirancang untuk menghubungkan perangkat fisik ke jaringan WiFi dan melakukan tugas otomatisasi atau pengiriman data sensor.

## Fitur Utama
- **Konektivitas WiFi**: Menghubungkan perangkat ke jaringan lokal secara otomatis.
- **Protokol Komunikasi**: Mendukung HTTP/MQTT (sesuaikan dengan kode Anda).
- **Integrasi Sensor/Aktuator**: Kontrol pin I/O untuk membaca data sensor atau mengendalikan relay.
- **Deep Sleep Mode**: Optimasi konsumsi daya untuk penggunaan baterai.

## Persyaratan Perangkat Keras
- Modul ESP8266 (NodeMCU, Wemos D1 Mini, atau ESP-01).
- Kabel Data Micro USB.
- Sensor/Komponen tambahan (sebutkan jika ada, misal: DHT11, LED, Relay).

## Persyaratan Perangkat Lunak
- [Arduino IDE](https://www.arduino.cc/en/software) (versi terbaru direkomendasikan).
- Board Package ESP8266 untuk Arduino IDE.
- Library tambahan:
  - `ESP8266WiFi`
  - `ESP8266HTTPClient`
  - (Tambahkan library lain yang Anda gunakan di sini).

## Cara Penggunaan
1. **Persiapan**: Buka Arduino IDE dan pastikan board ESP8266 sudah terinstal melalui *Boards Manager*.
2. **Konfigurasi Kredensial Aman**:
  - Salin `esp8266_dht22_oled_mqtt/credentials.example.h` menjadi `esp8266_dht22_oled_mqtt/credentials.h`.
  - Isi `credentials.h` dengan data WiFi/MQTT asli Anda.
  - File `credentials.h` sudah diabaikan oleh Git (`.gitignore`), jadi tidak ikut ter-upload ke GitHub.
3. **Instalasi Library**: Pastikan semua library yang dibutuhkan sudah terpasang melalui *Library Manager*.
4. **Upload**: Hubungkan ESP8266 ke komputer, pilih port yang sesuai, dan tekan tombol **Upload**.
5. **Monitoring**: Buka *Serial Monitor* dengan baud rate 115200 untuk melihat status perangkat.

## Keamanan Repository
- Jangan menyimpan SSID, password WiFi, host/port MQTT, atau username/password MQTT langsung di file `.ino`.
- Jika kredensial sempat ter-push ke GitHub, segera ganti password tersebut (rotate) meskipun commit sudah dihapus.

## Struktur Folder
- `/src`: Berisi kode sumber utama.
- `/docs`: Dokumentasi tambahan atau skema rangkaian.
- `/libraries`: Daftar library yang diperlukan.

## Kontribusi
Kontribusi selalu terbuka! Silakan lakukan *fork* pada repositori ini dan ajukan *pull request* jika ingin menambahkan fitur baru atau memperbaiki bug.

## Lisensi
Proyek ini dilisensikan di bawah [MIT License](LICENSE).