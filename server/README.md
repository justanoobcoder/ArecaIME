# Areca uinput server

Server Go nhận transaction `PLAN` từ addon Areca, tự gửi số lượng rewrite cộng
một `BackSpace` sentinel qua `/dev/uinput`, chờ theo plan rồi phát đúng một
`DONE`.

Addon không spawn server. Hãy chạy server bằng systemd/user service hoặc chạy
trực tiếp trước khi dùng fallback uinput. Nếu transport hỏng giữa transaction,
addon dừng pipeline để không retry một plan có thể đã thực thi một phần.

## Chạy thử

```bash
cd server
go run . -socket /tmp/openkey-nonpreedit.sock
```

Cũng có thể cấu hình socket bằng environment:

```bash
ARECA_UINPUT_SOCKET=/tmp/openkey-nonpreedit.sock go run .
```

Mặc định server cố chạy với process priority cao hơn (`nice -10`). Nếu không
có `CAP_SYS_NICE`, thao tác này fail best-effort và server vẫn chạy. Có thể tắt:

```bash
ARECA_UINPUT_SERVER_PRIORITY=0 go run .
```

Với binary cài tại `/usr/libexec`, có thể cấp riêng capability priority:

```bash
sudo setcap cap_sys_nice+ep /usr/libexec/areca-uinput-server
getcap /usr/libexec/areca-uinput-server
```

Mặc định server im lặng. Bật log runtime khi cần debug:

```bash
ARECA_UINPUT_SERVER_LOG=1 go run .
```

Server tạo virtual keyboard qua `/dev/uinput`, đăng ký keycode chuẩn `1..255`
để tránh GNOME/Mutter thu hẹp global keymap khi thiết bị ảo được thêm vào, và
xử lý `SIGTERM` để đóng thiết bị/xoá socket sạch sẽ.

## Protocol

Client gửi:

```text
PLAN <session> <tx> <backspaces> <inter_usec> <commit_delay_usec>
```

Với `backspaces = N`, server phát `N + 1` Backspace; event cuối là sentinel để
addon filter khi đường input của compositor đưa nó quay lại Fcitx. Server chỉ
trả sau khi tất cả event và `commit_delay_usec` đã hoàn tất:

```text
DONE <session> <tx>
```

Các biến `OPENKEY_NONPREEDIT_SERVER_*` cũ vẫn được hỗ trợ để tương thích.
