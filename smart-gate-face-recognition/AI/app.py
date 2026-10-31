from flask import Flask, Response, render_template_string
import cv2
import requests
import time
import numpy as np
import threading
import face_recognition
import os
import queue

app = Flask(__name__)

# --- 1. CAU HINH IP ---
ESP32_CAM_IP    = os.environ.get("ESP32_CAM_IP", "192.168.1.100")
ESP32_NORMAL_IP = os.environ.get("ESP32_NORMAL_IP", "192.168.1.101")   # ESP32 thường nhận tín hiệu

STREAM_URL   = f"http://{ESP32_CAM_IP}:81/stream"
GATE_URL     = f"http://{ESP32_CAM_IP}:80/open_gate"
SIGNAL_OK    = f"http://{ESP32_NORMAL_IP}/dung"    # Khuôn mặt đúng
SIGNAL_WRONG = f"http://{ESP32_NORMAL_IP}/sai"     # Khuôn mặt sai

# --- 2. HOC KHUON MAT CHU NHA ---
print("Dang nap du lieu khuon mat Chu Nha...")
if not os.path.exists("chunha.jpg"):
    print("LOI: Khong tim thay file 'chunha.jpg'!")
    exit()

chunha_image = face_recognition.load_image_file("chunha.jpg")
encodings = face_recognition.face_encodings(chunha_image)
if not encodings:
    print("LOI: Khong phat hien khuon mat trong 'chunha.jpg'!")
    exit()

known_face_encodings = [encodings[0]]
known_face_names     = ["Chu Nha"]
print("Da nap xong khuon mat: Chu Nha")

# --- 3. BIEN TOAN CUC ---
result_lock       = threading.Lock()
ai_frame_lock     = threading.Lock()
detection_results = []
last_open_time    = 0

# [FIX COOLDOWN] Tách riêng cooldown cho tín hiệu đúng và sai
#   last_ok_time   : cooldown 3s cho /dung (mở cổng → không cần gửi liên tục)
#   last_wrong_time: cooldown 5s cho /sai  (tránh wrongCount tăng quá nhanh
#                    khi AI liên tục thấy người lạ đứng trước camera)
last_ok_time      = 0
last_wrong_time   = 0

ai_latest_frame   = None

frame_queue = queue.Queue(maxsize=1)

# ==========================================================
# 🎥 LUỒNG 1: KÉO VIDEO TỪ ESP32 CAM
# ==========================================================
def camera_worker():
    global ai_latest_frame
    while True:
        print("Dang ket noi den Camera ESP32...")
        try:
            resp = requests.get(STREAM_URL, stream=True, timeout=5)
            print("Ket noi stream thanh cong!")
            bytes_data = b''

            for chunk in resp.iter_content(chunk_size=4096):
                if not chunk:
                    continue

                bytes_data += chunk
                a = bytes_data.find(b'\xff\xd8')
                b = bytes_data.find(b'\xff\xd9')

                if a != -1 and b != -1 and a < b:
                    jpg        = bytes_data[a : b + 2]
                    bytes_data = bytes_data[b + 2:]

                    frame = cv2.imdecode(
                        np.frombuffer(jpg, dtype=np.uint8),
                        cv2.IMREAD_COLOR
                    )

                    if frame is not None:
                        try:
                            frame_queue.put_nowait(frame)
                        except queue.Full:
                            try:
                                frame_queue.get_nowait()
                            except queue.Empty:
                                pass
                            frame_queue.put_nowait(frame)

                        with ai_frame_lock:
                            ai_latest_frame = frame

                elif len(bytes_data) > 32768:
                    next_start = bytes_data.rfind(b'\xff\xd8')
                    bytes_data = bytes_data[next_start:] if next_start != -1 else b''

        except Exception as e:
            print(f"Mat ket noi camera: {e}")
            time.sleep(1)

threading.Thread(target=camera_worker, daemon=True).start()

# ==========================================================
# 📡 HÀM GỬI TÍN HIỆU TỚI ESP32 THƯỜNG
# ==========================================================
def send_signal(url, label):
    def _send():
        try:
            requests.get(url, timeout=3)
            print(f"[SIGNAL] Da gui tin hieu '{label}' toi ESP32 thuong")
        except Exception as e:
            print(f"[SIGNAL] Loi gui tin hieu '{label}': {e}")
    threading.Thread(target=_send, daemon=True).start()

# ==========================================================
# 🧠 LUỒNG 2: AI NHAN DIEN KHUON MAT
# ==========================================================
def ai_worker():
    global detection_results, last_open_time, last_ok_time, last_wrong_time, ai_latest_frame
    while True:
        with ai_frame_lock:
            frame_to_process = ai_latest_frame.copy() if ai_latest_frame is not None else None

        if frame_to_process is None:
            time.sleep(0.1)
            continue

        small_frame     = cv2.resize(frame_to_process, (0, 0), fx=0.5, fy=0.5,
                                     interpolation=cv2.INTER_AREA)
        rgb_small_frame = cv2.cvtColor(small_frame, cv2.COLOR_BGR2RGB)

        face_locations      = face_recognition.face_locations(rgb_small_frame, model="hog")
        face_encodings_list = face_recognition.face_encodings(rgb_small_frame, face_locations)

        temp_results   = []
        found_owner    = False
        found_stranger = False

        for (top, right, bottom, left), face_encoding in zip(face_locations, face_encodings_list):
            # [FIX TOLERANCE] Tăng tolerance 0.50 → 0.55 để nhận diện tốt hơn
            #   trong điều kiện ánh sáng yếu, đội mũ, hoặc góc nghiêng
            #   (0.50 quá chặt → dễ miss chủ nhà → gửi /sai → wrongCount sai)
            matches = face_recognition.compare_faces(
                known_face_encodings, face_encoding, tolerance=0.55
            )
            name = "Nguoi La"
            if True in matches:
                name = known_face_names[matches.index(True)]

            temp_results.append((top*2, right*2, bottom*2, left*2, name))

            if name == "Chu Nha":
                found_owner = True
            else:
                found_stranger = True

        current_time = time.time()

        # [FIX COOLDOWN] Gửi tín hiệu tới ESP32 thường với cooldown riêng biệt
        #   /dung  : cooldown 3s — đủ để ESP32 xử lý và mở cổng
        #   /sai   : cooldown 5s — tránh wrongCount tăng quá nhanh nếu người lạ
        #            đứng trước camera liên tục (AI chạy mỗi 0.25s, không cooldown
        #            sẽ gửi /sai 12 lần/phút → trigger alarm nhầm)
        if found_owner:
            if (current_time - last_ok_time) > 3:
                send_signal(SIGNAL_OK, "dung")
                last_ok_time = current_time
        elif found_stranger:
            if (current_time - last_wrong_time) > 5:
                send_signal(SIGNAL_WRONG, "sai")
                last_wrong_time = current_time

        # Mở cổng ESP32 CAM khi nhận ra chủ nhà (cooldown 10 giây)
        if found_owner and (current_time - last_open_time > 10):
            def send_open_command():
                try:
                    print("!!! Xac nhan CHU NHA -> Gui lenh mo cong !!!")
                    requests.get(GATE_URL, timeout=3)
                except:
                    pass
            threading.Thread(target=send_open_command, daemon=True).start()
            last_open_time = current_time

        with result_lock:
            detection_results = temp_results

        time.sleep(0.25)

threading.Thread(target=ai_worker, daemon=True).start()

# ==========================================================
# 🌐 LUỒNG 3: STREAM LEN WEB
# ==========================================================
def gen_frames():
    last_good_frame = None

    while True:
        new_frame = None
        try:
            while True:
                new_frame = frame_queue.get_nowait()
        except queue.Empty:
            pass

        if new_frame is not None:
            last_good_frame = new_frame

        if last_good_frame is None:
            time.sleep(0.05)
            continue

        frame = last_good_frame.copy()

        with result_lock:
            cur_results = list(detection_results)

        for (top, right, bottom, left, name) in cur_results:
            color = (0, 255, 0) if name == "Chu Nha" else (0, 0, 255)
            cv2.rectangle(frame, (left, top), (right, bottom), color, 2)
            cv2.putText(frame, name, (left, top - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.9, color, 2)

        ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 60])
        if ret:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n'
                   + buffer.tobytes() + b'\r\n')

        time.sleep(0.033)

# ==========================================================
# 🌐 WEB ROUTES
# ==========================================================
@app.route('/')
def index():
    html = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>Bảng Điều Khiển Cổng Thông Minh</title>
        <style>
            body { text-align: center; font-family: Arial, sans-serif; background-color: #1a1a1a; color: white; margin: 0; padding: 0; }
            h1 { margin-top: 30px; color: #00d2ff; text-transform: uppercase; letter-spacing: 2px; }
            .video-container { margin-top: 20px; }
            img { border: 4px solid #00d2ff; border-radius: 12px; max-width: 90%; box-shadow: 0 0 20px rgba(0, 210, 255, 0.6); }
            .status { color: #00ff88; font-weight: bold; margin-top: 25px; font-size: 1.3em; letter-spacing: 1px; }
        </style>
    </head>
    <body>
        <h1>Hệ Thống An Ninh FaceID</h1>
        <div class="video-container">
            <img src="/video_feed" width="640" height="480">
        </div>
        <div class="status">⚡ Trạng thái: Hệ thống đang chạy ổn định...</div>
    </body>
    </html>
    """
    return render_template_string(html)

@app.route('/video_feed')
def video_feed():
    return Response(gen_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    print(f"\n=============================================")
    print(f" SERVER DA KHOI DONG!")
    print(f" Truy cap: http://127.0.0.1:5000")
    print(f"=============================================\n")
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
