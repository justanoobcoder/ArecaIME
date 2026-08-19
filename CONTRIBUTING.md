# Đóng góp cho Areca IME 🌴

Areca IME là bộ gõ tiếng Việt C++20 cho Fcitx5 trên Linux Wayland, sử dụng hàng đợi FIFO bất đồng bộ, tối ưu độ trễ microsecond và tính ổn định cao.

Mọi đóng góp báo lỗi, đề xuất ý tưởng, tối ưu thuật toán hoặc gửi Pull Request đều được hoan nghênh.

---

## 👥 Tác giả & Đồng hành

### Tác giả & Phát triển chính
- **Kim Xuân Hồng** ([@kimxuanhong](https://github.com/kimxuanhong) / [@xhkzeroone](https://github.com/xhkzeroone))

### Cộng đồng đóng góp

<a href="https://github.com/xhkzeroone/ArecaIME/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=xhkzeroone/ArecaIME" alt="Areca IME Contributors" />
</a>

---

## 🛠️ Hướng dẫn phát triển

### 1. Môi trường cần thiết
- Trình biên dịch C++20: GCC 11+ hoặc Clang 13+
- CMake >= 3.20 & Ninja
- Go 1.18+
- Thư viện Fcitx5 development

### 2. Biên dịch & Kiểm thử
```bash
git clone https://github.com/xhkzeroone/ArecaIME.git
cd ArecaIME
git submodule update --init --recursive

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 3. Quy trình gửi Pull Request
1. Fork repository và tạo nhánh mới từ `main`:
   ```bash
   git checkout -b feature/ten-tinh-nang
   ```
2. Đảm bảo mã nguồn tuân thủ tiêu chí không nghẽn main thread và vượt qua toàn bộ unit test.
3. Rebase với `main` và gửi Pull Request kèm mô tả chi tiết.

---

## 🐛 Báo lỗi & Đề xuất tính năng

1. Kiểm tra danh sách [Issues đã có](https://github.com/xhkzeroone/ArecaIME/issues) trước khi tạo mới.
2. Cung cấp thông tin Distro Linux, Desktop Environment và ứng dụng phát sinh lỗi.
3. Đính kèm log debug từ lệnh `journalctl --user -f | grep areca` nếu có.

---

## 📜 Giấy phép
Dự án phát hành dưới giấy phép mã nguồn mở [MIT License](LICENSE).
