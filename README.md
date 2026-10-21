# Băng tải phân loại sản phẩm

Ứng dụng Windows mô phỏng một dây chuyền phân loại sản phẩm bằng băng tải. Chọn loại sản phẩm, chạy băng tải và quan sát sản phẩm đi qua cảm biến, tiến theo encoder rồi được cần gạt hướng vào làn tương ứng.

## Phần mềm làm được gì?

- Thêm sản phẩm loại A, B, C hoặc không xác định.
- Chạy băng tải và theo dõi vị trí sản phẩm theo xung encoder.
- Chạy một lô sản phẩm với số lượng và kiểu phân loại đã chọn.
- Quan sát cần gạt chuyển hướng sản phẩm vào làn 1, làn 2, làn 3 hoặc làn loại bỏ.
- Xem bộ đếm kết quả và xuất lịch sử phân loại ra tệp CSV để mở bằng Excel.
- Mô phỏng lỗi cảm biến và tình huống encoder không tăng trong giao diện dòng lệnh.

| Loại sản phẩm | Làn đích |
|---|---|
| A | Làn 1 |
| B | Làn 2 |
| C | Làn 3 |
| Không xác định | Loại bỏ |

## Chạy trên Windows

### Yêu cầu

- Windows 10/11.
- CMake 3.20 trở lên.
- Một trình biên dịch C++17: MinGW-w64 hoặc Visual Studio với workload **Desktop development with C++**.

Mở PowerShell tại thư mục project trước khi chạy các lệnh bên dưới.

### Cách 1: MinGW-w64

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
.\build-mingw\smart_conveyor.exe
```

Sau lần cấu hình đầu tiên, khi sửa mã nguồn, chỉ cần biên dịch và chạy lại:

```powershell
cmake --build build-mingw
.\build-mingw\smart_conveyor.exe
```

### Cách 2: Visual Studio C++

Mở **Developer PowerShell for Visual Studio** tại thư mục project:

```powershell
cmake -S . -B build-msvc
cmake --build build-msvc --config Debug
.\build-msvc\Debug\smart_conveyor.exe
```

Sau lần cấu hình đầu tiên, biên dịch và chạy lại bằng:

```powershell
cmake --build build-msvc --config Debug
.\build-msvc\Debug\smart_conveyor.exe
```

> Nếu trình biên dịch báo không ghi được `smart_conveyor.exe` hoặc hiện `Permission denied`, hãy đóng cửa sổ ứng dụng đang chạy rồi biên dịch lại. Không dùng chung một thư mục build cho MinGW và Visual Studio.

## Cách sử dụng giao diện

1. Mở ứng dụng. Nhấn **Chạy băng tải** nếu muốn cho băng tải chạy trước.
2. Để thêm từng sản phẩm, chọn loại A/B/C rồi nhấn **Thêm sản phẩm**. Băng tải sẽ tự chạy nếu đang dừng.
3. Để chạy lô, nhập số lượng, chọn loại sản phẩm hoặc kiểu xen kẽ rồi nhấn **Bắt đầu phân loại**. Nút này đổi thành **Tạm dừng lô**; có thể tiếp tục lô sau khi tạm dừng.
4. Quan sát sơ đồ đường đi, trạng thái cần gạt và bộ đếm theo làn.
5. Nhấn **Dừng băng tải** để dừng. Nhấn **Xuất báo cáo CSV** để chọn nơi lưu kết quả.
6. Nhấn **Hướng dẫn** trong ứng dụng để xem lại hướng dẫn nhanh.

## Chạy giao diện dòng lệnh (tùy chọn)

Ứng dụng dòng lệnh được biên dịch cùng project trên Windows. Chạy một trong các lệnh phù hợp với cấu hình đã chọn:

```powershell
.\build-mingw\smart_conveyor_cli.exe
```

hoặc:

```powershell
.\build-msvc\Debug\smart_conveyor_cli.exe
```

Nhập `trogiup` để xem lệnh. Ví dụ, `them A` thêm một sản phẩm A, `tien 100` cho encoder tiến 100 xung, và `thongke` hiển thị thống kê. Có thể dùng `loi-cam-bien` hoặc `ket-bang-tai` để quan sát bộ điều khiển phản ứng với lỗi mô phỏng.

## Cách mô phỏng hoạt động

Khi cảm biến mô phỏng phát hiện sản phẩm, bộ điều khiển tạo ID và lưu vị trí encoder. Sản phẩm được đưa vào làn đã ánh xạ khi encoder tiến đến vị trí gạt. Trong giao diện Windows, tốc độ demo là 20 xung/giây, khoảng cách đến cần gạt mặc định là 100 xung và sản phẩm trong lô cách nhau 40 xung.

Các con số này chỉ phục vụ quan sát luồng hoạt động; chúng chưa được hiệu chuẩn theo kích thước hoặc tốc độ của một băng tải cụ thể. Kết quả phân loại được ghi khi chương trình ra lệnh cho cần gạt mô phỏng, chưa có cảm biến ở đầu ra để xác nhận sản phẩm đã thực sự vào đúng làn.

## Giới hạn khi dùng với thiết bị thật

Chương trình hiện dùng cảm biến, encoder, động cơ và cần gạt mô phỏng. Người dùng chọn loại sản phẩm; phần mềm chưa tự nhận dạng bằng camera hay mã vạch. Để triển khai trên dây chuyền thật cần bổ sung driver phù hợp với PLC và thiết bị, cấu hình tín hiệu, xác nhận sản phẩm tại đầu ra, liên động và dừng khẩn cấp, sau đó kiểm thử an toàn trên phần cứng.

## Cấu trúc project

```text
app/                         Giao diện Windows và giao diện dòng lệnh
src/domain/                  Dữ liệu sản phẩm, loại sản phẩm và làn đích
src/application/             Quy tắc điều khiển và phân loại
src/hal/interfaces/          Hợp đồng giao tiếp với thiết bị
src/platform/simulation/     Thiết bị mô phỏng
src/diagnostics/             Ghi nhật ký
config/                      Tài liệu cấu hình
docs/                        Tài liệu kiến trúc
tests/                       Tài liệu định hướng kiểm thử
```

Xem [tài liệu kiến trúc](docs/architecture.md) để biết chi tiết các thành phần và luồng xử lý.
