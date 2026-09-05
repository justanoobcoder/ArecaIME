# Cài đặt, reload và debug

## Kiểm tra build

```bash
cmake --build build
ctest --test-dir build --output-on-failure
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
areca: reliability first-probe force_forward=1 reason=program-compatibility-capability-mask-0x72
areca: selected uinput-shift-select backend for browser
areca: uinput-shift-select start tx=
areca: uinput-select split commit (1ms)
areca: browser autocomplete or active selection strategy=
areca: rewrite select backend=
areca: forward-backspace start
areca: forward-backspace sent
areca: forward-backspace complete
areca: rewrite done
areca: protected reset cancelled/executed
```

## Phân biệt lỗi backend

Khi log có:

```text
rewrite select backend=surrounding-text
```

Areca đang dùng `deleteSurroundingText()`. Kiểm tra first-probe word/shown nếu
xoá sai.

Khi log có:

```text
rewrite select backend=forward-backspace
```

Kiểm tra theo thứ tự:

1. `forward-backspace start` có đúng `backspaces` không.
2. Số dòng `forward-backspace sent` có đủ không.
3. `forward-backspace complete` có xuất hiện sau `after_wait_ms` không.
4. Sau complete có `rewrite done` và post-commit barrier không.

## Cấu hình cũ che default mới

Timing hiện được lưu trong
`~/.config/fcitx5/conf/areca-advanced.conf`. Ví dụ source đổi `ResetDelayMs`
mặc định thành 250 không tự sửa dòng `ResetDelayMs=120` đã tồn tại. Kiểm tra
trực tiếp:

```bash
grep -E '^(BackspaceDelayMs|AfterBackspaceWaitMs|WaylandAfterBackspaceWaitMs|PostCommitDelayMs|PreciseTiming|ResetDelayMs)=' \
  ~/.config/fcitx5/conf/areca-advanced.conf
```

Bản nâng cấp vẫn đọc các giá trị timing cũ trong `areca.conf` khi file
nâng cao chưa có. Sau khi mở và lưu panel **Cấu hình nâng cao**, hãy kiểm tra
file mới ở trên.

Sau khi sửa config, reload/restart Fcitx rồi kiểm tra log timing thực tế.
