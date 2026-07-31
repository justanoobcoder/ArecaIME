# Cài đặt, reload và debug

## Kiểm tra build

```bash
cmake --build build
ctest --test-dir build --output-on-failure
(cd server && go test ./...)
```

`build/areca.so` là addon vừa build. Với prefix `/usr`, bản được Fcitx load là
`/usr/lib/fcitx5/areca.so` trên hệ thống hiện tại.

## Xác nhận binary đã cài

```bash
ls -li --time-style=long-iso build/areca.so /usr/lib/fcitx5/areca.so
```

Timestamp/size giống nhau chỉ chứng minh file đã copy. Native addon đang chạy
vẫn có thể giữ inode cũ trong RAM.

```bash
fcitx_pid="$(pgrep -n -x fcitx5)"
grep 'areca\.so' "/proc/$fcitx_pid/maps"
```

Nếu output có `(deleted)`, installer đã thay file trên đĩa nhưng Fcitx hiện tại
vẫn chạy code cũ. Cần restart đúng process Fcitx.

## Plasma Wayland

Trên Plasma, KWin có thể trực tiếp spawn Fcitx:

```text
kwin_wayland → fcitx5
```

Process này nhận một `WAYLAND_SOCKET` riêng từ KWin. Vì vậy `fcitx5 -rd` chạy
từ terminal có thể không thay được instance đó dù command không báo lỗi rõ
ràng. Đăng xuất/đăng nhập luôn tạo process mới và chắc chắn nạp addon mới.

Kiểm tra quan hệ process:

```bash
ps -eo pid,ppid,lstart,comm,args | grep -E 'kwin_wayland|fcitx5'
```

Không cần logout nếu process Fcitx thực sự restart và PID đổi. Sau restart, chạy
lại lệnh `/proc/<pid>/maps` để xác nhận không còn `(deleted)`.

## Log addon

Bật:

```ini
Debug=True
```

Theo dõi log:

```bash
journalctl --user -f | grep areca
```

Những dòng hữu ích:

```text
areca: queue push
areca: scheduler process
areca: bamboo result
areca: reliability first-probe
areca: rewrite select backend=
areca: uinput prepare PLAN
areca: uinput DONE observed
areca: remote DONE
areca: protected reset cancelled/executed
```

## Log uinput server

```bash
systemctl --user status areca-uinput-server.service
journalctl --user -u areca-uinput-server -f
```

Restart server sau khi cài binary mới:

```bash
systemctl --user restart areca-uinput-server.service
```

Kiểm tra socket và process:

```bash
systemctl --user show areca-uinput-server.service -p MainPID -p ExecMainStatus
ls -l /tmp/openkey-nonpreedit.sock
```

## Quyền `/dev/uinput`

```bash
ls -l /dev/uinput
id
getent group uinput
```

Nếu installer vừa thêm user vào group `uinput`, session hiện tại chưa tự nhận
group mới. Đăng xuất/đăng nhập rồi kiểm tra lại `id`. Có thể kiểm tra capability
priority riêng của server:

```bash
getcap /usr/libexec/areca-uinput-server
```

`CAP_SYS_NICE` chỉ giúp server đặt priority tốt hơn; thiếu capability này không
ngăn uinput hoạt động. Quyền ghi `/dev/uinput` mới là điều bắt buộc.

## Phân biệt lỗi backend

Khi log có:

```text
rewrite select backend=surrounding-text
```

Areca đang dùng `deleteSurroundingText()`. Kiểm tra first-probe word/shown nếu
xoá sai.

Khi log có:

```text
rewrite select backend=uinput-socket
```

Kiểm tra theo thứ tự:

1. Server service có chạy không.
2. `SocketPath` hai bên có giống nhau không.
3. Server có mở `/dev/uinput` được không.
4. Addon có gửi `PLAN` không.
5. Có đủ Backspace thật cộng sentinel không.
6. Server có trả đúng `DONE session tx` không.

Nếu log báo pipeline paused sau transport failure, restart Fcitx để xoá
fail-closed state. Không retry transaction cũ bằng tay.

## Cấu hình cũ che default mới

Fcitx giữ file `~/.config/fcitx5/conf/areca.conf`. Ví dụ source đổi
`ResetDelayMs` mặc định thành 120 không tự sửa dòng `ResetDelayMs=250` đã tồn
tại. Kiểm tra trực tiếp:

```bash
grep -E '^(KeyIntervalMs|PostCommitDelayMs|ResetDelayMs|SocketPath)=' \
  ~/.config/fcitx5/conf/areca.conf
```

Sau khi sửa config, reload/restart Fcitx rồi kiểm tra log timing thực tế.
