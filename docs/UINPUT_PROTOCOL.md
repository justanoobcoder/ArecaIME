# Protocol Unix socket và uinput

Areca addon là client; `areca-uinput-server` là server. Hai bên giao tiếp bằng
Unix stream socket, mặc định tại `/tmp/areca-uinput.sock`. Mỗi message là
một dòng ASCII kết thúc bằng `\n`.

## PLAN

Client gửi:

```text
PLAN <session-id> <transaction-id> <backspace-count> <delay-us> <wait-us>
```

| Field | Kiểu | Ý nghĩa |
| --- | --- | --- |
| `session-id` | `uint64` | ID tạo theo lifetime của addon process, ngăn response cũ mở khoá session mới. |
| `transaction-id` | `uint64` | ID tăng dần cho mỗi rewrite. |
| `backspace-count` | integer không âm | Số Backspace thật cần đến ứng dụng. |
| `delay-us` | integer không âm | Khoảng chờ giữa hai Backspace, tính bằng microsecond. |
| `wait-us` | integer không âm | Timeout dự phòng sau toàn bộ Backspace; dùng nếu addon không gửi `WAIT`. |

Ví dụ xoá ba character, delay 5 ms và chờ 40 ms:

```text
PLAN 738850850354732 503 3 5000 40000
```

## Sentinel Backspace

Với `backspace-count = N`, server phát tổng cộng `N + 1` lần Backspace:

```text
Backspace #1 ... Backspace #N → ứng dụng
Backspace #(N+1)               → sentinel
```

Nếu compositor route virtual keyboard event qua Fcitx, addon đếm event và
filter press/release của sentinel. Nếu compositor đưa uinput thẳng tới client,
addon có thể không quan sát event; `DONE` vẫn là hàng rào hoàn tất chính thức.

## WAIT sau ACK full

Khi addon quan sát đủ `N + 1` Backspace, nó gửi:

```text
WAIT <session-id> <transaction-id> <delay-us>
```

Server huỷ phần chờ `wait-us` còn lại của `PLAN`, chờ theo `delay-us` của
`WAIT`, rồi trả `DONE`. `AckFullWaitMs` mặc định là 20 ms. Nếu addon không
quan sát đủ event, `PLAN` vẫn tự hoàn tất sau `AfterBackspaceWaitMs`, mặc định
40 ms.

## DONE

Server trả đúng một response sau timer được chọn: `WAIT delay-us` nếu nhận
được WAIT, nếu không thì dùng `PLAN wait-us`:

```text
DONE <session-id> <transaction-id>
```

Addon chỉ chấp nhận `DONE` khi:

- Opcode đúng.
- Session trùng process hiện tại.
- Có pending request.
- Transaction trùng pending request.

Sau `DONE`, addon commit `commitText`, cập nhật SurroundingText cache, clear
pending state và mới cho scheduler tiếp tục. Server không gửi ack trung gian;
"ACK full" là việc addon tự quan sát đủ `N + 1` KeyEvent Backspace quay lại.

## Ordering và lỗi

- Mỗi addon chỉ có một pending transaction.
- Server thực thi từng plan của một connection theo session runner.
- Addon không commit trước `DONE`.
- Dòng response sai hoặc cũ bị ignore.
- Lỗi trước khi PLAN được gửi hoàn chỉnh có thể recover bằng raw key.
- Lỗi sau khi PLAN đã gửi làm scheduler dừng fail-closed; client không retry vì
  server có thể đã phát một phần Backspace.

## Timing

Giá trị cấu hình addon dùng millisecond nhưng được đổi sang microsecond khi tạo
wire message:

```text
BackspaceDelayMs      × 1000 → delay-us
AfterBackspaceWaitMs × 1000 → wait-us
AckFullWaitMs         × 1000 → WAIT delay-us
```

`PostCommitDelayMs` không nằm trong protocol. Đây là timer phía addon, bắt đầu
sau khi `DONE` được nhận và text đã commit.

## Chạy server thủ công

```bash
cd server
ARECA_UINPUT_SERVER_LOG=1 go run . -socket /tmp/areca-uinput.sock
```

Server cần quyền ghi `/dev/uinput`. Installer tạo group/rule và user service
phù hợp. Xem thêm [DEBUGGING.md](DEBUGGING.md) và
[`server/README.md`](../server/README.md).
