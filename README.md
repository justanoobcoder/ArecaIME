# Areca IME

Areca là bộ gõ tiếng Việt cho Linux dưới dạng addon Fcitx5, viết bằng C++17 và
chạy chủ yếu trên Wayland. Areca dùng trực tiếp `bamboo-core` để xử lý tiếng
Việt, nhưng tự quản lý thời điểm xử lý phím và cách sửa nội dung đã hiển thị.

Điểm khác biệt chính của Areca là mọi **text key do addon xử lý** đều đi qua
hàng đợi FIFO. Không có phím nào được đưa vào Bamboo ngay trong callback
`keyEvent()`. Scheduler tuần tự hoá toàn bộ pipeline, giữ khoảng cách tối thiểu
20 ms giữa các lần xử lý và không cho phím sau chen vào một rewrite đang chờ
uinput server.

> Areca hiện là dự án thử nghiệm. Backend uinput có thể tạo phím Backspace thật
> ở cấp hệ thống; hãy đọc phần cài đặt và debug trước khi sử dụng thường xuyên.

## Lời cảm ơn

Areca sử dụng [Bamboo Engine / bamboo-core](https://github.com/BambooEngine/bamboo-core),
thư viện xử lý tiếng Việt do **Lương Thành Lâm** phát triển và phát hành theo
giấy phép MIT. Phần chuyển đổi Telex và các quy tắc tiếng Việt của Areca đến từ
chính `bamboo-core`; Areca chỉ bổ sung lớp tích hợp Fcitx5, scheduler và các
backend rewrite.

Xin cảm ơn tác giả Bamboo cùng những người được dự án Bamboo ghi công:

- Trung Ngo, tác giả `bogo.js`.
- Trần Kỳ Nam, tác giả `GoTiengViet`.

Mã nguồn và license nguyên bản được giữ trong submodule
[`bamboo/bamboo-core`](bamboo/bamboo-core).

## Ý tưởng thiết kế

Ứng dụng Wayland không phải lúc nào cũng cung cấp `SurroundingText` chính xác.
Trong khi đó, gõ tiếng Việt kiểu Telex thường cần sửa lại những ký tự đã xuất
hiện, ví dụ `a` + `w` biến thành `ă`. Areca giải quyết bài toán này bằng ba lớp:

1. `bamboo-core` quyết định chuỗi tiếng Việt mới.
2. Scheduler quyết định **khi nào** được xử lý phím tiếp theo.
3. Rewrite backend quyết định **cách** xoá chuỗi cũ và chèn chuỗi mới.

```text
Fcitx5 keyEvent
       │ filter text key
       ▼
 KeyQueue (FIFO)
       │ key đã chờ đủ KeyIntervalMs
       │ và không có transaction pending
       ▼
 InputScheduler ──► BambooEngineAdapter ──► BambooResult
                                             │
                         deleteCount == 0 ────┤──► unchanged: forwardKey
                                                  └──► transformed: commitString
                                             │
                         deleteCount > 0  ────┘
                                             │
                         ReliabilityChecker  │
                                  ┌──────────┴──────────┐
                                  ▼                     ▼
                       SurroundingTextBackend   UinputSocketBackend
                       delete + commit ngay     PLAN → chờ DONE → commit
```

Các invariant quan trọng:

- Queue giữ đúng thứ tự phím đầu vào.
- Chỉ một phím được Bamboo xử lý tại một thời điểm.
- Hai lần xử lý liên tiếp cách nhau ít nhất `KeyIntervalMs`.
- Chỉ có một rewrite uinput pending.
- Không commit text của rewrite uinput trước `DONE`.
- Phím mới vẫn được nhận vào queue khi server đang chạy plan, nhưng chưa được
  xử lý.
- Không `sleep()` trên main thread Fcitx5; timer và socket đều dùng event loop.
- Lỗi transport sau khi plan có thể đã được gửi làm pipeline dừng fail-closed,
  tránh gửi lặp và xoá hai lần.

Chi tiết từng component và state machine nằm trong
[Tài liệu kiến trúc](docs/ARCHITECTURE.md).

## Hai đường rewrite

### SurroundingText

Ở rewrite đầu tiên của mỗi input context, Areca so phần từ ngay trước cursor
với text mà Bamboo tin rằng đang hiển thị. Kết quả được cache cho ô nhập đó:

- Khớp: dùng `deleteSurroundingText()` rồi `commitString()`.
- Không khớp, snapshot không hợp lệ hoặc app không hỗ trợ capability: fallback
  sang uinput.

Areca cập nhật lại cache SurroundingText nội bộ sau delete và commit để lần xử
lý kế tiếp không đọc một snapshot cũ.

### Uinput socket

Khi SurroundingText không đáng tin cậy, addon gửi một plan qua Unix domain
socket. Server tự phát toàn bộ Backspace với timing được yêu cầu và chỉ trả một
lần `DONE`. Với yêu cầu xoá `N` ký tự, server phát `N + 1` Backspace: `N` phím
đầu đi tới ứng dụng, phím cuối là sentinel để addon filter nếu event quay lại
Fcitx.

```text
PLAN <session> <transaction> <backspaces> <delay-us> <wait-us>
DONE <session> <transaction>
```

`DONE` là kết quả có thẩm quyền. Một số compositor đưa event uinput thẳng tới
ứng dụng nên addon không nhất thiết quan sát được mọi Backspace. Vì vậy nếu
`DONE` tới trước, addon không chờ thêm ack event và tiếp tục pipeline an toàn.

Đặc tả đầy đủ nằm trong [Protocol uinput](docs/UINPUT_PROTOCOL.md).

## Bảo vệ state trước reset của ứng dụng

Một số ứng dụng gọi `reset()` nhiều lần trong lúc người dùng vẫn đang gõ. Areca
không xoá state ngay:

- Mỗi reset mở lại một quiet window `ResetDelayMs`, mặc định 120 ms.
- Text key mới trong cửa sổ đó huỷ reset.
- Nếu rewrite uinput đang pending, reset tiếp tục được hoãn.
- Chỉ khi không có input mới và không còn transaction pending, Bamboo state và
  queue của input context mới được reset.
- Verdict SurroundingText được giữ riêng vì nó mô tả độ tin cậy của ô nhập,
  không phải composition tạm thời.

Password field luôn bypass Bamboo, SurroundingText và uinput; phím gốc được
forward thẳng để không đưa nội dung nhạy cảm vào state của addon.

## Cài đặt nhanh

Installer hỗ trợ Arch/CachyOS, Debian/Ubuntu và Fedora. Script sẽ cài dependency,
khởi tạo submodule Bamboo, build, chạy test, cài addon cùng server, thiết lập
quyền `/dev/uinput` và tạo user service:

```bash
./scripts/install.sh
```

Nếu dependency đã có sẵn:

```bash
./scripts/install.sh --skip-deps
```

Một số lựa chọn khác:

```bash
./scripts/install.sh --user
./scripts/install.sh --socket /tmp/areca-uinput.sock
./scripts/install.sh --build-dir build-make
./scripts/install.sh --skip-tests
./scripts/install.sh --no-restart
```

Sau khi cài, mở `fcitx5-configtool` và thêm **Areca (Bamboo)** vào danh sách
input method.

Nếu installer vừa thêm user vào group `uinput`, cần đăng xuất/đăng nhập một lần
để session nhận group mới. Trên Plasma Wayland, KWin có thể là process trực tiếp
khởi chạy Fcitx bằng một `WAYLAND_SOCKET` riêng; khi đó `fcitx5 -rd` từ terminal
có thể không thay được instance đang giữ addon cũ. Xem cách xác nhận và xử lý
trong [Hướng dẫn debug](docs/DEBUGGING.md).

## Build thủ công

Yêu cầu:

- CMake 3.20 trở lên.
- Compiler hỗ trợ C++17.
- Go.
- Fcitx5 Core, Config và Utils development packages.
- Ninja hoặc Make.

```bash
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
(cd server && go test ./...)
sudo cmake --install build
```

Nếu thư mục `build` cũ dùng Ninja nhưng máy hiện tại không còn Ninja, dùng một
build directory mới thay vì tái sử dụng cache khác generator:

```bash
cmake -S . -B build-make -G "Unix Makefiles"
cmake --build build-make
```

Build mặc định tạo `areca.so` và `areca-uinput-server`. Có thể không build
server bằng `-DBUILD_UINPUT_SERVER=OFF` nếu đã có server khác tương thích
protocol.

## Cấu hình

Mở cấu hình Areca trong `fcitx5-configtool`. Các tuỳ chọn thường dùng nằm ngay
trong màn hình chính và được lưu tại `~/.config/fcitx5/conf/areca.conf`:

```ini
BambooInputMethod=Telex 2
OutputCharset=Unicode
SpellCheck=True
ModernStyle=True
AutoCapitalizeAfterPunctuation=False
EnableMacro=True
CapitalizeMacro=True
Debug=True
```

Chọn **Cấu hình nâng cao** để mở panel timing/socket riêng. Các giá trị trong
panel này được lưu tại `~/.config/fcitx5/conf/areca-advanced.conf`:

```ini
KeyIntervalMs=20
BackspaceDelayMs=5
AfterBackspaceWaitMs=10
PostCommitDelayMs=20
ResetDelayMs=120
SocketPath=/tmp/areca-uinput.sock
```

| Panel | Tuỳ chọn | Ý nghĩa |
| --- | --- | --- |
| Chính | `BambooInputMethod` | Tên input method được định nghĩa bởi Bamboo, mặc định `Telex 2`. |
| Chính | `OutputCharset` | Bảng mã do Bamboo cung cấp, mặc định `Unicode`; gồm Unicode dựng sẵn/tổ hợp cùng các bảng mã tương thích cũ như TCVN3, VNI Windows, VIQR… |
| Chính | `SpellCheck` | Dùng bộ kiểm tra cấu trúc âm tiết của Bamboo; tại word boundary, tự khôi phục từ tiếng Việt không hợp lệ về chuỗi phím Latin ban đầu. |
| Chính | `ModernStyle` | `True` đặt dấu kiểu `hoà`, `thuý`; `False` dùng kiểu `hòa`, `thúy`. |
| Chính | `AutoCapitalizeAfterPunctuation` | Tự viết hoa chữ ASCII đầu tiên sau `.`, `!`, `?` và khoảng trắng; `Enter` kích hoạt trực tiếp. Nhiều khoảng trắng vẫn giữ trạng thái chờ. |
| Chính | `EnableMacro` | Bật thay thế từ viết tắt tại dấu cách hoặc dấu câu. |
| Chính | `CapitalizeMacro` | Tự đổi nội dung macro thành chữ thường/toàn chữ hoa theo cách viết key. |
| Chính | `Debug` | Bật log chi tiết của addon. |
| Nâng cao | `KeyIntervalMs` | Thời gian tối thiểu một key phải nằm trong queue và khoảng cách tối thiểu giữa hai lần xử lý Bamboo; không thể nhỏ hơn 20 ms. |
| Nâng cao | `BackspaceDelayMs` | Delay giữa hai Backspace do uinput server phát. |
| Nâng cao | `AfterBackspaceWaitMs` | Thời gian server chờ sau Backspace cuối trước khi trả `DONE`. |
| Nâng cao | `PostCommitDelayMs` | Settling window sau mọi text commit, tính từ `DONE` đối với uinput. |
| Nâng cao | `ResetDelayMs` | Quiet window bảo vệ state trước reset từ ứng dụng. |
| Nâng cao | `SocketPath` | Đường dẫn Unix socket dùng chung giữa addon và server. |

Lưu ý: đổi giá trị mặc định trong source không ghi đè file cấu hình đã tồn tại.
Khi nâng cấp từ bản cũ, Areca tự đọc timing/socket còn nằm trong `areca.conf`;
nếu `areca-advanced.conf` đã tồn tại thì file mới luôn được ưu tiên. Hãy sửa file
người dùng hoặc dùng giao diện cấu hình Fcitx5 rồi reload addon.
Danh sách `BambooInputMethod` và `OutputCharset` trong giao diện được lấy động
từ `bamboo-core`, không được hard-code trong addon.

## Macro

Mở cấu hình Areca trong `fcitx5-configtool`, chọn **Chỉnh sửa macro** rồi thêm
các cặp `Từ viết tắt → Nội dung thay thế`. Macro được lookup không phân biệt
hoa/thường và chỉ expand khi gặp word boundary, nên key vẫn có thể được gõ như
một phần của từ dài hơn.

Ví dụ với macro `bt → Be There` và `CapitalizeMacro=True`:

```text
bt<Space>  → be there␠
BT<Space>  → BE THERE␠
Bt<Space>  → Be There␠
```

Expansion chạy trước spell-check, được encode theo `OutputCharset`, sau đó đi
qua cùng scheduler và rewrite backend như kết quả Bamboo bình thường. Dữ liệu
được lưu tại `~/.config/fcitx5/conf/areca-macro-table.conf`.

Areca giữ lại composition Bamboo của từ vừa chốt. Vì vậy sau khi Backspace qua
dấu cách hoặc dấu câu, có thể tiếp tục sửa dấu hay xoá từ vừa gõ thay vì Bamboo
coi đó là một từ hoàn toàn mới.

## Trạng thái và giới hạn hiện tại

- Mode hiện tại là queued rewrite trực tiếp vào ứng dụng; chưa có giao diện
  preedit-only hoàn chỉnh.
- Reliability là heuristic một lần cho mỗi input context, không phải chứng minh
  tuyệt đối rằng mọi snapshot tương lai đều đúng.
- Uinput phụ thuộc `/dev/uinput`, quyền group và cách compositor route virtual
  keyboard event.
- Nếu kết nối socket hỏng sau khi plan đã được gửi một phần, Areca cố ý dừng
  pipeline thay vì đoán và retry.
- Native addon `.so` không hot reload. File mới có thể đã cài nhưng process
  Fcitx hiện tại vẫn map inode cũ trong RAM.

## Tài liệu

- [Kiến trúc và luồng hoạt động](docs/ARCHITECTURE.md)
- [Protocol Unix socket/uinput](docs/UINPUT_PROTOCOL.md)
- [Cài đặt, reload và debug](docs/DEBUGGING.md)
- [Tài liệu riêng của uinput server](server/README.md)

## License

Areca được phát hành theo giấy phép [MIT](LICENSE). `bamboo-core` là một dự án
độc lập và giữ license/copyright riêng tại
[`bamboo/bamboo-core/LICENSE`](bamboo/bamboo-core/LICENSE).
