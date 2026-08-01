# Kiến trúc Areca IME

Tài liệu này mô tả pipeline hiện tại của Areca. Mục tiêu thiết kế là giữ đúng
thứ tự input, tránh rewrite song song và cô lập engine tiếng Việt khỏi chi tiết
Wayland, Fcitx5 và uinput.

## Thành phần

| Thành phần | Trách nhiệm |
| --- | --- |
| `ArecaEngine` | Nhận event từ Fcitx5, phân loại text/special/password key, quản lý state theo input context và reset barrier. |
| `InputState` | Giữ một Bamboo adapter, verdict SurroundingText và timer reset riêng cho từng input context. |
| `InputModeHandler` | Điểm mở rộng cho cách trình bày input. Hiện có `QueuedRewriteMode`. |
| `KeyQueue` | FIFO chứa key gốc, Unicode codepoint, UTF-8, sequence và reference tới input context. |
| `InputScheduler` | Timer 20 ms, single-flight processing, backend selection và transaction barrier. |
| `BambooEngineAdapter` | Bridge C++/Go gọi trực tiếp `bamboo-core` và biến chuỗi kết quả thành `BambooResult`. |
| `ReliabilityChecker` | Probe SurroundingText lần đầu và cache verdict theo input context. |
| `RewriteBackend` | Interface chung cho thao tác apply một `RewritePlan`. |
| `SurroundingTextBackend` | Gọi `deleteSurroundingText()` và `commitString()`. |
| `UinputSocketBackend` | Nonblocking Unix socket, pending state, sentinel filter và commit sau `DONE`. |
| `PendingRewriteState` | Giữ transaction, input context, commit text và tiến độ event của đúng một rewrite. |

## Phân tách cấu hình

Cấu hình chính ở `conf/areca.conf` chỉ chứa các tuỳ chọn dùng thường xuyên.
Timing và Unix socket nằm trong sub-config **Cấu hình nâng cao** tại
`conf/areca-advanced.conf`, dùng cùng cơ chế panel con với trình sửa macro.
Các field timing/socket cũ trong `areca.conf` được giữ ẩn để migrate cấu hình;
file nâng cao, nếu có, luôn được load sau và được ưu tiên.

## Vòng đời text key

1. Fcitx5 gọi `ArecaEngine::keyEvent()`.
2. Release event bình thường không bị giữ lại. Modifier-only key được bỏ qua.
3. Password input được forward thẳng và state cũ của context bị xoá.
4. Special key đi theo policy riêng; text key hợp lệ được `filterAndAccept()`.
5. Areca huỷ delayed reset đang chờ rồi đẩy key vào `KeyQueue`.
6. Scheduler đặt timer dựa trên cả thời điểm enqueue và lần xử lý gần nhất.
7. Khi timer chạy, scheduler pop đúng một key và gọi Bamboo.
8. Scheduler apply `BambooResult`; chỉ sau khi apply hoàn tất mới mở phím kế.

Một text key không bao giờ được Bamboo xử lý inline trong callback Fcitx. Điều
này tránh trường hợp một app hoặc compositor đưa key vào nhanh hơn khả năng
rewrite ổn định của backend.

## Công thức scheduling

Với `interval = KeyIntervalMs`, deadline của key đầu queue là:

```text
max(now,
    key.enqueuedAt + interval,
    lastProcessedAt + interval)
```

Do đó:

- Key luôn nằm trong queue ít nhất `interval`.
- Hai Bamboo call liên tiếp luôn cách nhau ít nhất `interval`.
- Nếu user gõ nhanh, key tích lại trong FIFO.
- Nếu user gõ chậm, scheduler vẫn không xử lý trước khi key đủ tuổi.

Sau apply, `PostCommitDelayMs` tạo thêm một settling window. Với uinput, window
này bắt đầu từ lúc nhận `DONE` và commit text, không bắt đầu từ Bamboo call cũ.

## BambooResult và diff

Go bridge tạo một `bamboo.IEngine` trực tiếp bằng:

```go
bamboo.NewEngine(method, bamboo.EstdFlags)
```

`BambooEngineAdapter` giữ chuỗi mà nó tin rằng đang hiển thị. Sau mỗi key,
Bamboo trả chuỗi đã xử lý mới; adapter tìm common prefix UTF-8 và tạo:

```text
currentText  chuỗi trước key
newText      chuỗi Bamboo mới
deleteCount  số Unicode character ở suffix cũ cần xoá
commitText   suffix mới cần chèn
```

Ví dụ Telex:

```text
currentText = "a"
key         = "w"
newText     = "ă"
deleteCount = 1
commitText  = "ă"
```

Ký tự Bamboo không xử lý được được xem như word boundary. Nếu `SpellCheck`
được bật, bridge gọi `IsValid(true)` trước khi reset. Một từ có ký tự tiếng
Việt nhưng cấu trúc âm tiết không hợp lệ được `RestoreLastWord(false)` về chuỗi
phím Latin ban đầu; adapter tạo delta rewrite cho phần restore rồi nối boundary.
Từ hợp lệ chỉ commit boundary như bình thường. Tính năng này dùng luật có sẵn
của `bamboo-core`, chưa dùng dictionary ngoài.

Khi tạo engine, bridge ánh xạ `ModernStyle=True` sang cách đặt dấu `oà/uý` và
`ModernStyle=False` sang `òa/úy`. Việc ánh xạ ngược với tên flag nội bộ
`EstdToneStyle` là có chủ ý để tên option mô tả trực tiếp chuỗi đầu ra.

Bridge cũng xuất danh sách input method và charset trực tiếp từ `bamboo-core`
cho giao diện cấu hình. Adapter giữ Bamboo state ở Unicode nội bộ, nhưng encode
chuỗi mới bằng `OutputCharset` trước khi tính common prefix. Vì vậy
`currentText`, `newText`, `deleteCount` và `commitText` luôn mô tả đúng chuỗi
thực tế đã commit vào ứng dụng, kể cả `Unicode tổ hợp`, VNI Windows hoặc VIQR.

## Macro expansion

Macro table là sub-config riêng gồm các cặp `Key/Value`. Khi xử lý một word
boundary, bridge lấy word hiện tại ở `PunctuationMode`, lookup key không phân
biệt hoa/thường và expand trước spell-check. Nếu `CapitalizeMacro` bật, key viết
thường tạo replacement viết thường, key toàn chữ hoa tạo replacement toàn chữ
hoa, còn kiểu mixed-case giữ nguyên value cấu hình. Adapter reset Bamboo sau
match, nối boundary, encode theo charset rồi tính delta rewrite. Vì thế macro
không có đường commit đặc biệt và vẫn tuân thủ mọi queue/pending invariant.

Nếu `AutoCapitalizeAfterPunctuation` bật, state theo từng input context theo dõi
`.`, `!`, `?` rồi khoảng trắng, hoặc `Enter`. Chữ ASCII thường kế tiếp được đổi
thành keysym hoa trước khi enqueue và trước khi Bamboo xử lý. Phím đã đổi hoa
vẫn đi qua scheduler như mọi text key khác và mang cờ buộc `commitString`, vì
replay phím vật lý không có Shift có thể vẫn tạo chữ thường. Reset, Backspace,
di chuyển con trỏ và shortcut sẽ xoá trạng thái chờ để tránh viết hoa nhầm.

## Backend selection

Khi `deleteCount == 0`:

- Nếu `commitText` giống text của phím gốc, scheduler replay phím bằng
  `forwardKey()` đủ press/release và cập nhật cache SurroundingText bằng text
  của phím đó.
- Nếu Bamboo đã biến đổi output dù không cần xoá, scheduler commit
  `commitText`. Trường hợp điển hình là dấu của `Unicode tổ hợp`.
- Cả hai nhánh vẫn đi qua queue và settling barrier.

Khi `deleteCount > 0`:

1. `ReliabilityChecker` đánh giá input context.
2. Verdict reliable chọn `SurroundingTextBackend`.
3. Verdict unreliable chọn `UinputSocketBackend`.
4. Browser inline-autocomplete luôn ép fallback và cộng một Backspace thật để
   dọn phần suggestion đang được chọn.

## Reliability lifetime

Probe chỉ diễn ra khi rewrite đầu tiên cần delete:

- App phải quảng bá capability `SurroundingText`.
- Snapshot phải valid.
- Từ ngay trước cursor phải có suffix không rỗng khớp với `currentText`.

Kết quả được cache trong `InputState`. Những rewrite sau dùng verdict đó thay vì
đưa thêm live guard làm thay đổi backend giữa một phiên gõ. Khi Areca được
activate lại cho input context, verdict được mở để context mới có thể probe.

Snapshot browser autocomplete không được dùng làm first probe; checker giữ
`known=false` để lần rewrite bình thường sau mới quyết định reliability.

## Uinput single-flight state machine

```text
Idle
 │ apply(plan)
 ▼
Connecting/Writing ── unsent error ──► recover raw key
 │ PLAN sent
 ▼
Pending ── transport error ──► Stalled (fail-closed)
 │ DONE đúng session + tx
 ▼
Commit text → clear pending → post-commit timer → process next key
```

Trong `Pending`, `processing_` vẫn là true nên scheduler không lấy key tiếp.
Key mới chỉ được append vào queue. Response sai session/transaction bị bỏ qua.

Nếu socket hỏng trước khi plan được gửi, addon biết chắc server chưa xoá gì và
có thể forward raw key. Nếu plan đã gửi, addon không biết server thực hiện tới
đâu, vì vậy retry hoặc commit đều có nguy cơ làm hỏng text; pipeline dừng lại.

## Surrounding cache

Cả hai rewrite backend đều cập nhật object `surroundingText()` mà Fcitx đang
cache:

- Sau delete: xoá đúng `cacheDeleteCount` character.
- Sau commit: chèn text tại cursor và cập nhật cursor/anchor.

`cacheDeleteCount` không bao gồm Backspace autocomplete bổ sung hoặc sentinel,
vì hai event đó không đại diện cho Bamboo display state.

## Reset barrier

`ArecaEngine::reset()` chỉ arm timer, không reset ngay. Reset thật xảy ra sau
`ResetDelayMs` nếu không có text input mới. Text key huỷ timer; pending uinput
transaction làm timer được arm lại. Điều này bảo vệ Bamboo composition, queue
và backend state khỏi reset nhiễu của ứng dụng.

Special key mà user chủ động gõ vẫn có policy tức thời:

- Cursor, Tab, Escape và Ctrl/Alt/Super/Meta combination: reset Bamboo rồi
  forward.
- Return/KP Enter: reset Bamboo rồi forward nguyên event.
- Backspace: gọi `RemoveLastChar(true)` để đồng bộ Bamboo rồi forward.
- Delete: forward mà không thay Bamboo history.

## Hướng mở rộng

`InputModeHandler` nằm phía trên scheduler. Một mode `PreeditOnlyMode` có thể
thay cách hiển thị mà không nhét nhánh preedit vào Bamboo adapter. Tương tự,
policy `SurroundingOnly` có thể cung cấp backend selector luôn chọn
`SurroundingTextBackend`; queue và scheduler không cần viết lại.
