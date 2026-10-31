# Smart Gate — Face Recognition-Based IoT Access Control

An academic team project combining ESP32 gate automation, ESP32-CAM video streaming, Python facial recognition, and Firebase remote commands and status updates.

## Team and individual contribution

Developed by a team of five for an IoT course in 2026.

**Pham Van Nhiem — Embedded Software & System Integration Developer:** software development, ESP32/ESP32-CAM firmware, Python recognition service, Firebase communication, hardware assembly, integration, debugging, and functional testing.

Team members: Nguyen Tran Khanh Nguyen, Pham Van Nhiem, Vo Truong Quoc Quang, Nguyen Quang Quan, and Pham REDACTED.

## Architecture

ESP32-CAM streams MJPEG over local Wi-Fi to a Python/Flask service. The service detects faces using HOG and matches face encodings against a locally supplied reference image. It sends HTTP requests to the main ESP32, which controls a servo, LCD, LED, and buzzer. The controller also polls Firebase for remote commands and writes timestamped status updates.

Face recognition runs on the Python host, not on the ESP32-CAM.

## Features implemented in the included source

- QVGA 320 × 240 MJPEG camera stream at port 81.
- Registered-user recognition and unknown-face signals.
- Flask video interface with recognition overlays.
- Servo gate control and automatic closing after 10 seconds.
- Infrared-triggered opening and I2C LCD status display.
- Alarm after three unknown-face signals; timer-based LED/buzzer toggling.
- Firebase command polling every 1,500 ms and current-status updates.

## Repository structure

- `AI/app.py`: Python recognition and video service.
- `ESP32_CAM/`: AI Thinker ESP32-CAM firmware.
- `MAIN_ESP/`: main ESP32 controller firmware.
- `requirements.txt`: Python dependencies (versions are not pinned).
- `docs/`: supplied report and course research paper.

## Hardware

ESP32 development board, AI Thinker ESP32-CAM, servo motor, infrared sensor, 16×2 I2C LCD, LED, and buzzer.

Main-controller pins from the source:

| Device | GPIO |
| --- | --- |
| Servo | 12 |
| LED | 26 |
| Buzzer | 27 |
| Infrared sensor | 13 |
| LCD SDA / SCL | 25 / 33 |

## Setup

1. Install Arduino IDE with ESP32 board support. Install `ESP32Servo` and a compatible `LiquidCrystal_I2C` library. The original ESP32 core/library versions were not supplied; compatibility may require adjustment.
2. In each firmware folder, copy `config.example.h` to `config.h` and enter your Wi-Fi settings. In `MAIN_ESP/config.h`, also configure your own Firebase URL and credential. Local configuration files are ignored by Git.
3. Open each `.ino` sketch, choose the matching board, and upload it to the corresponding device. Obtain device IP addresses from the serial monitor at 115200 baud.
4. On the Python host, create a virtual environment and install dependencies:

```bash
python -m venv .venv
python -m pip install -r requirements.txt
```

Activate the environment before installing/running. The `face-recognition` dependency uses dlib and may require CMake and a C++ build toolchain.

5. Add your own authorized face reference as `AI/chunha.jpg`. No face image is included.
6. Set `ESP32_CAM_IP` and `ESP32_NORMAL_IP` environment variables to your device addresses, or adjust the fallback addresses in `AI/app.py`.
7. Run from the AI directory:

```bash
cd AI
python app.py
```

8. Open `http://127.0.0.1:5000` on the Python host. Keep the devices and host on the same reachable local network.

Firebase configuration/rules must be provisioned separately. The firmware uses `smart_gate/lenh_dieu_khien` for commands and `smart_gate/trang_thai_thuc` for status. The separate dashboard/admin app source is not included.

## Performance study

The supplied course paper describes tracing an operation with a `trace_id` and timestamps t0–t9. One recorded sample reports:

| Stage | Latency |
| --- | ---: |
| Command delivery to ESP32 | 1,231 ms |
| ESP32 reception to execution start | 0 ms at recorded resolution |
| Preparation for Firebase write | 64 ms |
| Firebase write | 857 ms |
| Web/App update | 1,612 ms |
| Total | 3,764 ms |

These are results reported in a single sample, not a measured average or a guarantee for the included code. Web/App updates account for 42.8% of this trace. Polling and synchronization were identified as optimization targets; push/event communication remains proposed work.

**Source/documentation scope:** this archive contains the original supplied source. It does not include the later `trace_id`/t0–t9 instrumentation, latency dashboard, or separate Web/App code described in the documents. Those results cannot be reproduced from this repository alone.

## Known limitations

- Python also attempts a legacy `/open_gate` request on camera port 80, but the supplied camera firmware exposes only `/stream` on port 81. The implemented gate-control path is Python → main ESP32 `/dung`.
- Face recognition uses one reference image; no anti-spoofing/liveness detection or measured recognition accuracy is provided.
- The local control endpoints have no authentication. This is an academic prototype for a trusted test network.
- The original firmware disables brownout detection; reliable servo power and common ground are required.
- No firmware compilation or physical hardware validation was performed during repository packaging.

## Documentation and demo

- [Project report](docs/PROJECT_REPORT.docx)
- [Course research paper](docs/SCIENTIFIC_PAPER.pdf) — course project manuscript; no publication status is claimed.
- [Demo video](https://youtu.be/WiDgIWuMQVg)

## Public configuration

Wi-Fi settings and Firebase credentials have been removed from tracked source. The original face image is excluded. Rotate any credential previously shared in the original archive before reuse.

## Upload to GitHub

Create an empty repository, extract this ZIP, then upload the **contents of this folder** using GitHub's Add file → Upload files, including both firmware folders, `AI`, `docs`, README, requirements, and `.gitignore`.

Alternatively, from this folder:

```bash
git init
git add .
git commit -m "Add Smart Gate academic project"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/smart-gate-face-recognition.git
git push -u origin main
```

Replace `YOUR_USERNAME` with your GitHub account. No license is assigned in this package; agree on reuse terms with the project team before adding one.
