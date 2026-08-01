# Areca uinput server

`areca-uinput-server` là tiến trình hỗ trợ cho backend fallback của Areca IME.
Khi một ứng dụng Wayland không cung cấp SurroundingText đáng tin cậy, addon gửi
một kế hoạch xoá qua Unix domain socket. Server thực thi toàn bộ kế hoạch bằng
`/dev/uinput` rồi trả đúng một kết quả `DONE`.

Server không xử lý tiếng Việt và không quyết định nội dung cần commit. Mọi quyết
định Bamboo, backend selection và text mới đều nằm trong addon Fcitx5. Nhiệm vụ
của server chỉ là phát Backspace đúng số lượng, đúng timing và báo khi plan đã
hoàn tất.

```text
Areca addon                  areca-uinput-server             Application
     │                               │                            │
     │ PLAN session/tx/N/timing      │                            │
     ├──────────────────────────────►│                            │
     │                               ├── Backspace × N ──────────►│
     │                               ├── sentinel Backspace       │
     │                               └── wait                     │
     │ DONE session/tx               │                            │
     │◄──────────────────────────────┤                            │
     │ commit text                   │                            │
     └───────────────────────────────────────────────────────────►│
```

## Nguyên tắc hoạt động

- Addon không tự spawn server; server phải chạy trước khi backend uinput được
  chọn.
- Mỗi plan mang `session-id` và `transaction-id` để response cũ không mở khoá
  transaction mới.
- Với yêu cầu xoá `N` ký tự, server phát `N + 1` Backspace. Phím cuối là
  sentinel để addon filter nếu compositor đưa event quay lại Fcitx.
- Server không gửi ack trung gian. `DONE` chỉ được gửi sau tất cả Backspace và
  thời gian chờ cuối plan.
- Nếu connection đóng hoặc plan bị thay thế, goroutine đang chạy được cancel.
- Thiết bị uinput được mở lazy ở plan đầu tiên và dùng chung qua mutex.
- `SIGTERM`/`SIGINT` đóng listener, huỷ session, destroy virtual keyboard và xoá
  socket.

Đặc tả wire protocol đầy đủ nằm tại
[`docs/UINPUT_PROTOCOL.md`](../docs/UINPUT_PROTOCOL.md).

## Cài đặt cùng Areca

Cách khuyên dùng là chạy installer từ thư mục gốc:

```bash
./scripts/install.sh
```

Build mặc định cài binary vào `libexec` và tạo user service:

```text
~/.config/systemd/user/areca-uinput-server.service
```

Installer cũng thực hiện các bước cần thiết cho `/dev/uinput`:

- Load module `uinput` nếu cần.
- Tạo group `uinput`.
- Thêm user hiện tại vào group.
- Cài udev rule với mode `0660`.
- Enable và start user service.

Nếu user vừa được thêm vào group, cần đăng xuất/đăng nhập một lần để session
nhận membership mới.

## Quản lý user service

```bash
systemctl --user status areca-uinput-server.service
systemctl --user restart areca-uinput-server.service
systemctl --user stop areca-uinput-server.service
journalctl --user -u areca-uinput-server -f
```

Unit do installer tạo dùng binary cài trong prefix và truyền cùng `SocketPath`
với addon. Installer luôn daemon-reload rồi restart service để cả binary lẫn
socket path mới có hiệu lực ngay.

## Build và test riêng server

Server chỉ phụ thuộc Go standard library:

```bash
cd server
go test ./...
go build -o areca-uinput-server .
```

Test hiện tại xác nhận một plan xoá `N` ký tự tạo đúng `N + 1` Backspace, bao
gồm sentinel, và chỉ phát một `DONE`.

## Chạy thủ công

Từ thư mục `server`:

```bash
ARECA_UINPUT_SERVER_LOG=1 \
go run . -socket /tmp/areca-uinput.sock
```

Hoặc dùng binary đã build:

```bash
ARECA_UINPUT_SERVER_LOG=1 \
./areca-uinput-server -socket /tmp/areca-uinput.sock
```

Đường dẫn socket phải giống `SocketPath` trong
`~/.config/fcitx5/conf/areca-advanced.conf`. Không chạy đồng thời hai server trên cùng
socket; process khởi động sau sẽ xoá socket path cũ trước khi listen.

## Tuỳ chọn dòng lệnh

```text
-socket PATH
    Unix socket path. Mặc định lấy từ ARECA_UINPUT_SOCKET, nếu không có thì
    dùng /tmp/areca-uinput.sock.

-priority=true|false
    Thử đặt process nice = -10. Mặc định true.
```

Ví dụ tắt priority mode:

```bash
go run . -priority=false
```

## Biến môi trường

| Biến | Ý nghĩa | Mặc định |
| --- | --- | --- |
| `ARECA_UINPUT_SOCKET` | Unix socket path nếu không truyền `-socket`. | `/tmp/areca-uinput.sock` |
| `ARECA_UINPUT_SERVER_LOG` | Bật log runtime. | `false` |
| `ARECA_UINPUT_SERVER_DEBUG` | Alias bật log runtime. | `false` |
| `ARECA_UINPUT_SERVER_PRIORITY` | Cho phép thử đặt nice `-10`. | `true` |

Các giá trị boolean được nhận diện gồm `1/0`, `true/false`, `yes/no` và
`on/off`.

Một số tên biến môi trường cũ vẫn được source chấp nhận để tương thích ngược,
nhưng cấu hình mới nên chỉ dùng prefix `ARECA_`.

## Priority và capability

Priority cao hơn không bắt buộc để gửi uinput. Nếu process không có quyền đặt
nice âm, server ghi log rồi tiếp tục chạy bình thường.

Với prefix hệ thống thường dùng:

```bash
sudo setcap cap_sys_nice+ep /usr/libexec/areca-uinput-server
getcap /usr/libexec/areca-uinput-server
```

`CAP_SYS_NICE` chỉ dành cho scheduling priority. Quyền ghi `/dev/uinput` vẫn do
group/udev rule quản lý.

## Virtual keyboard

Server tạo một thiết bị có tên:

```text
Areca IME Uinput Server
```

Thiết bị đăng ký `EV_KEY`, `EV_SYN` và các keycode chuẩn `1..255`. Việc đăng ký
đầy đủ giúp tránh trường hợp desktop environment thu hẹp global keymap khi một
virtual keyboard chỉ khai báo duy nhất Backspace. Sau khi tạo thiết bị, server
chờ 50 ms để compositor nhận diện trước khi phát event đầu tiên.

## Protocol tóm tắt

Addon gửi một dòng:

```text
PLAN <session> <tx> <backspaces> <inter-usec> <final-wait-usec>
```

Ví dụ xoá ba ký tự, cách nhau 5 ms và chờ 10 ms:

```text
PLAN 738850850354732 503 3 5000 10000
```

Server phát bốn Backspace, trong đó phím thứ tư là sentinel, rồi trả:

```text
DONE 738850850354732 503
```

`DONE` là barrier hoàn tất. Addon không commit text rewrite trước response này.
Không dựa vào số event quay lại Fcitx vì một số compositor route uinput thẳng
tới application.

## Log và chẩn đoán

Bật log khi chạy thủ công:

```bash
ARECA_UINPUT_SERVER_LOG=1 go run .
```

Hoặc với service:

```bash
systemctl --user edit areca-uinput-server.service
```

Thêm:

```ini
[Service]
Environment=ARECA_UINPUT_SERVER_LOG=1
```

Sau đó:

```bash
systemctl --user daemon-reload
systemctl --user restart areca-uinput-server.service
journalctl --user -u areca-uinput-server -f
```

Một transaction bình thường có các mốc:

```text
client connected
recv PLAN ...
start plan ...
inject backspace ...
inject sentinel ...
emit done ...
send DONE ...
```

Nếu không có `inject`:

1. Kiểm tra addon và server có cùng socket path.
2. Kiểm tra service đang chạy.
3. Kiểm tra protocol error trong log.

Nếu có `uinput backspace failed`:

```bash
ls -l /dev/uinput
id
getent group uinput
```

Nếu server có `send DONE` nhưng addon không tiếp tục, đối chiếu `session` và
`tx` ở log hai bên. Addon cố ý bỏ response sai transaction.

## Xử lý lỗi và an toàn

- Không chạy server bằng root nếu group/udev rule đã được cấu hình đúng.
- Không đặt socket thành world-writable. Bất kỳ client nào kết nối được đều có
  thể yêu cầu server phát Backspace.
- Server xoá socket path lúc khởi động và khi shutdown sạch; một process khác
  không nên dùng chung path.
- Nếu server chết giữa plan, addon có thể dừng pipeline fail-closed vì không
  biết bao nhiêu Backspace đã tới application. Restart cả server và Fcitx trước
  khi tiếp tục thử.
- Không tự retry một transaction cũ: retry có thể xoá text hai lần.

Xem thêm hướng dẫn tổng thể tại
[`docs/DEBUGGING.md`](../docs/DEBUGGING.md).
