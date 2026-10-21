#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "application/ConveyorController.h"
#include "diagnostics/ILogger.h"
#include "platform/simulation/MockDiverter.h"
#include "platform/simulation/MockEncoder.h"
#include "platform/simulation/MockMotor.h"
#include "platform/simulation/MockPhotoelectricSensor.h"

namespace {
enum ControlId {
    ID_PRODUCT_TYPE = 100,
    ID_ADD_PRODUCT,
    ID_STOP,
    ID_START,
    ID_BATCH_QUANTITY,
    ID_BATCH_PATTERN,
    ID_BATCH_TOGGLE,
    ID_EXPORT_REPORT,
    ID_BATCH_PROGRESS,
    ID_CONVEYOR_VIEW,
    ID_HELP,
};

constexpr UINT_PTR BATCH_TIMER_ID = 1;
constexpr UINT ANIMATION_TIMER_INTERVAL_MS = 16;

HWND g_window{};
HWND g_mainTitle{};
HWND g_status{};
std::array<HWND, 5> g_statValues{};
HWND g_log{};
HWND g_typeCombo{};
HWND g_batchQuantityInput{};
HWND g_batchPatternCombo{};
HWND g_batchButton{};
HWND g_batchProgress{};
HWND g_conveyorView{};
HFONT g_uiFont{};
HFONT g_titleFont{};
HFONT g_numberFont{};

MockPhotoelectricSensor g_sensor;
MockEncoder g_encoder;
MockMotor g_motor;
MockDiverter g_diverter;

class WindowLogger final : public ILogger {
public:
    explicit WindowLogger(HWND output) : output_(output) {}

    void info(const std::string& message) override { append("[THONG TIN] " + message); }
    void error(const std::string& message) override { append("[LOI] " + message); }

private:
    void append(const std::string& message) {
        if (!IsWindow(output_)) return;
        const int length = GetWindowTextLengthW(output_);
        SendMessageW(output_, EM_SETSEL, length, length);
        const std::wstring wide(message.begin(), message.end());
        SendMessageW(output_, EM_REPLACESEL, FALSE,
            reinterpret_cast<LPARAM>((wide + L"\r\n").c_str()));
    }

    HWND output_{};
};

std::unique_ptr<WindowLogger> g_logger;
std::unique_ptr<ConveyorController> g_controller;
bool g_batchInProgress{};
bool g_batchPaused{};
int g_batchTotal{};
int g_batchCreated{};
int g_lastBatchCompleted{};
std::uint64_t g_autoBatchStarts{};
int g_batchPattern{};
constexpr int g_batchSpacing{40};
int g_batchTicksSinceProduct{};
constexpr int g_batchTicksPerSecond{20};
using SteadyClock = std::chrono::steady_clock;
SteadyClock::time_point g_lastFrameTime{};
bool g_frameClockStarted{};
double g_tickRemainder{};
double g_visualEncoderPosition{};

void startAnimationTimer() {
    g_lastFrameTime = SteadyClock::now();
    g_frameClockStarted = true;
    SetTimer(g_window, BATCH_TIMER_ID, ANIMATION_TIMER_INTERVAL_MS, nullptr);
}

struct DivertedProductVisual {
    ProductType type{ProductType::UNKNOWN};
    LaneId lane{LaneId::REJECT_LANE};
    std::int64_t startedAt{};
};
std::vector<DivertedProductVisual> g_divertedVisuals;
std::size_t g_lastVisualRecordCount{};
struct PanelDecoration { RECT bounds{}; std::wstring title; };
std::vector<PanelDecoration> g_panels;

void refreshView();

void collectDivertedProducts() {
    const auto& records = g_controller->sortingRecords();
    while (g_lastVisualRecordCount < records.size()) {
        const auto& record = records[g_lastVisualRecordCount++];
        g_divertedVisuals.push_back({record.type, record.lane, record.sortedAtTicks});
    }
}

bool readPositiveNumber(HWND control, long long minimum, long long maximum, long long& result) {
    wchar_t value[32]{};
    GetWindowTextW(control, value, static_cast<int>(std::size(value)));
    wchar_t* end = nullptr;
    const long long parsed = std::wcstoll(value, &end, 10);
    if (end == value || *end != L'\0' || parsed < minimum || parsed > maximum) return false;
    result = parsed;
    return true;
}

ProductType selectedProductType() {
    const LRESULT selection = SendMessageW(g_typeCombo, CB_GETCURSEL, 0, 0);
    switch (selection) {
    case 0: return ProductType::TYPE_A;
    case 1: return ProductType::TYPE_B;
    case 2: return ProductType::TYPE_C;
    default: return ProductType::UNKNOWN;
    }
}

ProductType batchProductType(int index) {
    switch (g_batchPattern) {
    case 0: return ProductType::TYPE_A;
    case 1: return ProductType::TYPE_B;
    case 2: return ProductType::TYPE_C;
    case 3: return index % 3 == 0 ? ProductType::TYPE_A :
                   index % 3 == 1 ? ProductType::TYPE_B : ProductType::TYPE_C;
    default: return ProductType::UNKNOWN;
    }
}

void injectProduct(ProductType type) {
    g_sensor.setDetected(true, type);
    g_controller->update();
    g_sensor.setDetected(false);
    g_controller->update();
    collectDivertedProducts();
}

void completeBatch() {
    g_batchInProgress = false;
    g_batchPaused = false;
    g_lastBatchCompleted = g_batchTotal;
    g_logger->info(std::string("Hoan tat lo ") + std::to_string(g_batchTotal) +
        " san pham.");
}

void toggleBatch() {
    if (g_batchInProgress && !g_batchPaused) {
        KillTimer(g_window, BATCH_TIMER_ID);
        g_batchPaused = true;
        g_controller->shutdown();
        g_logger->info("Da tam dung lo tu dong");
        return;
    }
    if (g_batchInProgress && g_batchPaused) {
        if (g_controller->state() == ControllerState::FAULT) {
            MessageBoxW(g_window, L"Băng tải đang báo lỗi. Hãy xử lý lỗi trước khi tiếp tục.", L"Băng tải chưa sẵn sàng", MB_OK | MB_ICONWARNING);
            return;
        }
        g_controller->initialize();
        g_batchPaused = false;
        startAnimationTimer();
        g_logger->info("Tiep tuc lo tu dong");
        return;
    }
    long long quantity = 0;
    if (!readPositiveNumber(g_batchQuantityInput, 1, 1000, quantity)) {
        MessageBoxW(g_window, L"Nhập số lượng sản phẩm từ 1 đến 1000.",
            L"Số lượng không hợp lệ", MB_OK | MB_ICONWARNING);
        return;
    }

    if (g_controller->state() == ControllerState::FAULT) {
        MessageBoxW(g_window, L"Băng tải đang báo lỗi. Hãy xử lý lỗi trước khi chạy lô.",
            L"Không thể bắt đầu", MB_OK | MB_ICONWARNING);
        return;
    }
    if (g_controller->state() != ControllerState::RUNNING) {
        g_controller->initialize();
        startAnimationTimer();
    }

    g_batchTotal = static_cast<int>(quantity);
    g_batchPattern = static_cast<int>(SendMessageW(g_batchPatternCombo, CB_GETCURSEL, 0, 0));
    if (g_batchPattern < 0) g_batchPattern = 0;
    g_batchCreated = 0;
    g_batchTicksSinceProduct = 0;
    g_batchInProgress = true;
    g_batchPaused = false;
    ++g_autoBatchStarts;
    injectProduct(batchProductType(g_batchCreated));
    ++g_batchCreated;
    startAnimationTimer();
    g_logger->info("Bat dau lo tu dong " + std::to_string(g_batchTotal) + " san pham");
}

void runSimulationTimer() {
    if (!g_controller || g_controller->state() != ControllerState::RUNNING) {
        KillTimer(g_window, BATCH_TIMER_ID);
        if (g_batchInProgress) {
            g_batchPaused = true;
            g_logger->error("Lo tu dong da tam dung do bang tai bao loi");
        }
        refreshView();
        return;
    }

    const auto now = SteadyClock::now();
    if (!g_frameClockStarted) {
        g_lastFrameTime = now;
        g_frameClockStarted = true;
    }
    const double elapsedSeconds = std::clamp(
        std::chrono::duration<double>(now - g_lastFrameTime).count(), 0.0, 0.1);
    g_lastFrameTime = now;
    g_tickRemainder += elapsedSeconds * g_batchTicksPerSecond;
    const int ticksToAdvance = static_cast<int>(g_tickRemainder);
    g_tickRemainder -= ticksToAdvance;

    for (int i = 0; i < ticksToAdvance && g_controller->state() == ControllerState::RUNNING; ++i) {
        g_encoder.addTicks(1);
        g_controller->update();
        collectDivertedProducts();
        if (g_batchInProgress && !g_batchPaused && g_batchCreated < g_batchTotal &&
            ++g_batchTicksSinceProduct >= g_batchSpacing) {
            injectProduct(batchProductType(g_batchCreated));
            ++g_batchCreated;
            g_batchTicksSinceProduct = 0;
        }
    }

    g_visualEncoderPosition = static_cast<double>(g_controller->encoderPosition()) + g_tickRemainder;

    const auto currentPosition = g_controller->encoderPosition();
    g_divertedVisuals.erase(std::remove_if(g_divertedVisuals.begin(), g_divertedVisuals.end(),
        [currentPosition](const DivertedProductVisual& product) {
            return currentPosition - product.startedAt > 20;
        }), g_divertedVisuals.end());

    if (g_batchInProgress && g_batchCreated == g_batchTotal &&
        g_controller->pendingProducts().empty()) completeBatch();
    refreshView();
}

void exportReport() {
    wchar_t path[MAX_PATH] = L"bao_cao_phan_loai.csv";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"Tệp CSV (*.csv)\0*.csv\0Tất cả tệp (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;

    std::ofstream file{std::filesystem::path(path)};
    if (!file) {
        MessageBoxW(g_window, L"Không thể tạo tệp báo cáo.", L"Lỗi xuất báo cáo", MB_OK | MB_ICONERROR);
        return;
    }
    file << "ma_san_pham,loai,lan,xung_phat_hien,xung_phan_loai\n";
    for (const auto& record : g_controller->sortingRecords()) {
        file << record.id << ',' << ConveyorController::productTypeName(record.type) << ','
             << ConveyorController::laneName(record.lane) << ',' << record.detectedAtTicks << ','
             << record.sortedAtTicks << '\n';
    }
    const auto& stats = g_controller->statistics();
    file << "\nchi_tieu,gia_tri\n"
         << "da_phat_hien," << stats.detected << '\n'
         << "lan_1," << stats.lane1 << '\n'
         << "lan_2," << stats.lane2 << '\n'
         << "lan_3," << stats.lane3 << '\n'
         << "loai_bo," << stats.rejected << '\n'
         << "so_loi," << stats.faults << '\n'
         << "so_lo_tu_dong_da_khoi_dong," << g_autoBatchStarts << '\n';
    file.close();
    MessageBoxW(g_window, L"Đã xuất danh sách phân loại và thống kê.", L"Xuất báo cáo thành công", MB_OK | MB_ICONINFORMATION);
}

HWND addControl(const wchar_t* type, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0) {
    if (std::wstring(type) == L"BUTTON" && (style == BS_PUSHBUTTON || style == BS_GROUPBOX))
        style = (style & ~static_cast<DWORD>(BS_TYPEMASK)) | BS_OWNERDRAW;
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

void addLabel(const wchar_t* text, int x, int y, int width, int height = 24) {
    addControl(L"STATIC", text, 0, x, y, width, height);
}

void addPanel(const wchar_t* title, int x, int y, int width, int height) {
    g_panels.push_back({RECT{x, y, x + width, y + height}, title});
}

void showHelp() {
    MessageBoxW(g_window,
        L"HƯỚNG DẪN NHANH\n\n"
        L"1. Chọn loại sản phẩm rồi nhấn Thêm sản phẩm, hoặc nhập số lượng để chạy một lô.\n"
        L"2. Băng tải tự khởi động khi bạn thêm sản phẩm hoặc bắt đầu lô.\n"
        L"3. Theo dõi sản phẩm đi từ cảm biến, qua cần gạt rồi vào làn nhận tương ứng.\n"
        L"4. Nhấn Dừng băng tải khi muốn kết thúc. Báo cáo CSV lưu danh sách và kết quả phân loại.\n\n"
        L"Đây là bản chạy trên máy tính, chưa kết nối PLC hay thiết bị thật.",
        L"Hướng dẫn sử dụng", MB_OK | MB_ICONINFORMATION);
}

bool setTextIfChanged(HWND control, const std::wstring& text) {
    if (!control) return false;
    const int length = GetWindowTextLengthW(control);
    std::wstring current(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, current.data(), length + 1);
    current.resize(static_cast<std::size_t>(copied));
    if (current == text) return false;

    SetWindowTextW(control, text.c_str());
    InvalidateRect(control, nullptr, FALSE);
    return true;
}

void refreshView() {
    if (!g_controller) return;

    const auto state = g_controller->state();
    const std::wstring faultText = g_controller->fault() == FaultCode::SENSOR_INVALID
        ? L"Cảm biến báo dữ liệu không hợp lệ" : L"Băng tải có thể đang bị kẹt";
    const std::wstring status = state == ControllerState::FAULT
        ? L"Cần xử lý: " + faultText
        : state == ControllerState::RUNNING ? L"Băng tải đang chạy · Sẵn sàng nhận sản phẩm"
                                           : L"Băng tải đang dừng";
    setTextIfChanged(g_status, status);

    const auto& stats = g_controller->statistics();
    const std::array<std::uint64_t, 5> values{stats.detected, stats.lane1, stats.lane2,
        stats.lane3, stats.rejected};
    for (std::size_t i = 0; i < values.size(); ++i)
        setTextIfChanged(g_statValues[i], std::to_wstring(values[i]));

    const std::wstring progress = g_batchInProgress
        ? L"Đang xử lý " + std::to_wstring(g_batchCreated) + L"/" + std::to_wstring(g_batchTotal)
        : g_lastBatchCompleted > 0
            ? L"Hoàn tất " + std::to_wstring(g_lastBatchCompleted) + L" sản phẩm"
            : L"Chưa có lô đang chạy";
    setTextIfChanged(g_batchProgress, progress);
    setTextIfChanged(g_batchButton,
        g_batchInProgress ? (g_batchPaused ? L"Tiếp tục lô" : L"Tạm dừng lô") : L"Bắt đầu phân loại");

    if (g_conveyorView) InvalidateRect(g_conveyorView, nullptr, FALSE);
}

void handleCommand(int id) {
    switch (id) {
    case ID_ADD_PRODUCT: {
        if (g_batchInProgress) {
            MessageBoxW(g_window, L"Hãy tạm dừng hoặc chờ lô hoàn tất trước khi thêm thủ công.", L"Lô đang chạy", MB_OK | MB_ICONINFORMATION);
            break;
        }
        if (g_controller->state() == ControllerState::FAULT) {
            MessageBoxW(g_window, L"Băng tải đang báo lỗi. Hãy xử lý lỗi trước khi thêm sản phẩm.",
                L"Không thể thêm sản phẩm", MB_OK | MB_ICONWARNING);
            break;
        }
        if (g_controller->state() != ControllerState::RUNNING) {
            g_controller->initialize();
            startAnimationTimer();
        }
        injectProduct(selectedProductType());
        g_logger->info("Da them mot san pham vao bang tai");
        break;
    }
    case ID_BATCH_TOGGLE:
        toggleBatch();
        break;
    case ID_EXPORT_REPORT:
        exportReport();
        break;
    case ID_HELP:
        showHelp();
        break;
    case ID_STOP:
        if (g_batchInProgress) g_batchPaused = true;
        KillTimer(g_window, BATCH_TIMER_ID);
        g_controller->shutdown();
        break;
    case ID_START:
        if (g_controller->state() == ControllerState::FAULT) {
            MessageBoxW(g_window, L"Hãy dừng băng tải và khởi động lại sau khi đã xử lý nguyên nhân lỗi.", L"Băng tải đang báo lỗi", MB_OK | MB_ICONWARNING);
        } else {
            g_controller->initialize();
            startAnimationTimer();
        }
        break;
    }
    refreshView();
}

void createInterface(HWND window) {
    g_window = window;
    g_mainTitle = addControl(L"STATIC", L"BĂNG TẢI PHÂN LOẠI", SS_LEFT, 20, 10, 520, 36);
    SendMessageW(g_mainTitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_titleFont), TRUE);
    g_status = addControl(L"STATIC", L"Băng tải đang dừng", SS_LEFT, 22, 47, 560, 22);
    addControl(L"BUTTON", L"Chạy băng tải", BS_PUSHBUTTON, 674, 17, 132, 38, ID_START);
    addControl(L"BUTTON", L"Dừng băng tải", BS_PUSHBUTTON, 814, 17, 134, 38, ID_STOP);
    addControl(L"BUTTON", L"Hướng dẫn", BS_PUSHBUTTON, 814, 59, 134, 30, ID_HELP);

    addPanel(L"BẮT ĐẦU TRONG 3 BƯỚC", 15, 96, 950, 54);
    addLabel(L"1  Chọn sản phẩm     2  Thêm sản phẩm hoặc chạy lô     3  Theo dõi sản phẩm vào đúng làn", 29, 122, 900, 22);

    addPanel(L"THÊM SẢN PHẨM", 15, 156, 950, 82);
    addLabel(L"Loại sản phẩm", 30, 181, 130);
    g_typeCombo = addControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
        30, 201, 150, 125, ID_PRODUCT_TYPE);
    SendMessageW(g_typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại A"));
    SendMessageW(g_typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại B"));
    SendMessageW(g_typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại C"));
    SendMessageW(g_typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Không rõ · loại bỏ"));
    SendMessageW(g_typeCombo, CB_SETCURSEL, 0, 0);
    addControl(L"BUTTON", L"Thêm sản phẩm", BS_PUSHBUTTON, 195, 199, 150, 36, ID_ADD_PRODUCT);
    addLabel(L"Băng tải tự chạy khi cần; sản phẩm sẽ được đưa vào làn tương ứng.", 365, 204, 570, 24);

    addPanel(L"CHẠY LÔ TỰ ĐỘNG", 15, 246, 950, 90);
    addLabel(L"Số lượng", 30, 271, 105);
    g_batchQuantityInput = addControl(L"EDIT", L"20", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
        30, 291, 105, 30, ID_BATCH_QUANTITY);
    addLabel(L"Loại sản phẩm", 150, 271, 170);
    g_batchPatternCombo = addControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
        150, 291, 200, 140, ID_BATCH_PATTERN);
    SendMessageW(g_batchPatternCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại A"));
    SendMessageW(g_batchPatternCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại B"));
    SendMessageW(g_batchPatternCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại C"));
    SendMessageW(g_batchPatternCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Xen kẽ A · B · C"));
    SendMessageW(g_batchPatternCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Loại bỏ"));
    SendMessageW(g_batchPatternCombo, CB_SETCURSEL, 3, 0);
    g_batchButton = addControl(L"BUTTON", L"Bắt đầu phân loại", BS_PUSHBUTTON,
        385, 288, 190, 38, ID_BATCH_TOGGLE);
    g_batchProgress = addControl(L"STATIC", L"Chưa có lô đang chạy", SS_LEFT,
        595, 296, 340, 22, ID_BATCH_PROGRESS);

    addPanel(L"KẾT QUẢ PHÂN LOẠI", 15, 344, 950, 88);
    const wchar_t* statNames[] = {L"Đã tiếp nhận", L"Làn 1", L"Làn 2", L"Làn 3", L"Loại bỏ"};
    const int statX[] = {30, 215, 400, 585, 770};
    for (std::size_t i = 0; i < g_statValues.size(); ++i) {
        addLabel(statNames[i], statX[i], 371, 160, 22);
        g_statValues[i] = addControl(L"STATIC", L"0", SS_CENTER, statX[i], 390, 150, 38);
        SendMessageW(g_statValues[i], WM_SETFONT, reinterpret_cast<WPARAM>(g_numberFont), TRUE);
    }
    addPanel(L"ĐƯỜNG ĐI CỦA SẢN PHẨM", 15, 440, 950, 148);
    g_conveyorView = addControl(L"STATIC", L"", SS_OWNERDRAW, 27, 466, 926, 111, ID_CONVEYOR_VIEW);
    addLabel(L"Lưu kết quả để mở bằng Excel", 22, 594, 300, 25);
    addControl(L"BUTTON", L"Xuất báo cáo CSV", BS_PUSHBUTTON, 765, 590, 185, 36, ID_EXPORT_REPORT);

    g_log = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_READONLY,
        0, 0, 1, 1, g_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_logger = std::make_unique<WindowLogger>(g_log);
    g_controller = std::make_unique<ConveyorController>(
        g_sensor, g_encoder, g_motor, g_diverter, *g_logger, 100, 10);
    refreshView();
}

void drawConveyorView(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT bounds = item->rcItem;
    const int height = bounds.bottom - bounds.top;
    HBRUSH background = CreateSolidBrush(RGB(249, 251, 255));
    FillRect(dc, &bounds, background);
    DeleteObject(background);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(71, 85, 105));
    RECT heading{bounds.left + 10, bounds.top + 4, bounds.right - 10, bounds.top + 20};
    DrawTextW(dc, L"SẢN PHẨM DI CHUYỂN TRÊN BĂNG TẢI", -1, &heading, DT_LEFT | DT_SINGLELINE);

    const int beltLeft = bounds.left + 25;
    const int beltRight = bounds.left + 397;
    const int beltY = bounds.top + height / 2 + 14;
    const int sensorX = beltLeft + 26;
    const int gateX = beltRight - 24;
    const int destinationDistance = g_controller
        ? static_cast<int>(std::max<std::int64_t>(1, g_controller->destinationDistanceTicks())) : 100;
    RECT belt{beltLeft, beltY - 12, beltRight, beltY + 15};
    HBRUSH beltBrush = CreateSolidBrush(RGB(51, 65, 85));
    FillRect(dc, &belt, beltBrush);
    DeleteObject(beltBrush);

    if (g_controller && g_controller->state() == ControllerState::RUNNING) {
        const double beltTravel = g_visualEncoderPosition *
            (gateX - sensorX) / std::max(1.0, static_cast<double>(destinationDistance));
        const int stripeOffset = static_cast<int>(std::fmod(beltTravel, 24.0));
        HPEN stripePen = CreatePen(PS_SOLID, 2, RGB(100, 116, 139));
        HGDIOBJ previousStripePen = SelectObject(dc, stripePen);
        for (int x = beltLeft + stripeOffset; x < beltRight; x += 24) {
            MoveToEx(dc, x, beltY - 8, nullptr);
            LineTo(dc, x, beltY + 10);
        }
        SelectObject(dc, previousStripePen);
        DeleteObject(stripePen);
    }

    HPEN rollerPen = CreatePen(PS_SOLID, 2, RGB(148, 163, 184));
    HGDIOBJ oldPen = SelectObject(dc, rollerPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    for (int x = beltLeft + 10; x < beltRight; x += 29)
        Ellipse(dc, x - 6, beltY + 11, x + 6, beltY + 23);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(rollerPen);

    HPEN sensorPen = CreatePen(PS_SOLID, 2, RGB(14, 165, 233));
    oldPen = SelectObject(dc, sensorPen);
    MoveToEx(dc, sensorX, bounds.top + 45, nullptr);
    LineTo(dc, sensorX, beltY - 14);
    SelectObject(dc, oldPen);
    DeleteObject(sensorPen);
    SetTextColor(dc, RGB(2, 132, 199));
    RECT sensorLabel{sensorX - 42, bounds.top + 25, sensorX + 46, bounds.top + 44};
    DrawTextW(dc, L"CẢM BIẾN", -1, &sensorLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT actuator{gateX - 8, bounds.top + 27, gateX + 8, beltY - 10};
    HBRUSH actuatorBrush = CreateSolidBrush(RGB(100, 116, 139));
    FillRect(dc, &actuator, actuatorBrush);
    DeleteObject(actuatorBrush);
    RECT actuatorCap{gateX - 19, bounds.top + 23, gateX + 19, bounds.top + 34};
    HBRUSH capBrush = CreateSolidBrush(RGB(51, 65, 85));
    FillRect(dc, &actuatorCap, capBrush);
    DeleteObject(capBrush);

    const LaneId selectedLane = g_diverter.selectedLane();
    const COLORREF bladeColor = g_diverter.isActive() ? RGB(249, 115, 22) : RGB(100, 116, 139);
    HBRUSH bladeBrush = CreateSolidBrush(bladeColor);
    HPEN bladePen = CreatePen(PS_SOLID, 1, bladeColor);
    HGDIOBJ savedBrush = SelectObject(dc, bladeBrush);
    oldPen = SelectObject(dc, bladePen);
    POINT bladePoints[4]{};
    if (g_diverter.isActive()) {
        bladePoints[0] = POINT{gateX - 5, beltY - 12};
        bladePoints[1] = POINT{gateX + 3, beltY - 12};
        const int deflect = selectedLane == LaneId::LANE_1 ? -27 :
            selectedLane == LaneId::LANE_2 ? -9 : selectedLane == LaneId::LANE_3 ? 10 : 25;
        bladePoints[2] = POINT{gateX - 30, beltY + deflect};
        bladePoints[3] = POINT{gateX - 39, beltY + deflect};
    } else {
        bladePoints[0] = POINT{gateX - 4, beltY - 15};
        bladePoints[1] = POINT{gateX + 4, beltY - 15};
        bladePoints[2] = POINT{gateX + 5, beltY - 38};
        bladePoints[3] = POINT{gateX - 3, beltY - 38};
    }
    Polygon(dc, bladePoints, 4);
    Ellipse(dc, gateX - 6, beltY - 18, gateX + 6, beltY - 6);
    SelectObject(dc, oldPen);
    SelectObject(dc, savedBrush);
    DeleteObject(bladePen);
    DeleteObject(bladeBrush);

    if (g_controller) {
        const double current = g_visualEncoderPosition;
        const int travelWidth = gateX - sensorX;
        for (const auto& product : g_controller->pendingProducts()) {
            const double travelled = std::clamp(current - product.positionTicks,
                0.0, static_cast<double>(destinationDistance));
            const int x = sensorX + static_cast<int>(travelWidth * travelled / destinationDistance);
            COLORREF productColor = RGB(37, 99, 235);
            wchar_t type = L'A';
            switch (product.type) {
            case ProductType::TYPE_A: productColor = RGB(37, 99, 235); type = L'A'; break;
            case ProductType::TYPE_B: productColor = RGB(22, 163, 74); type = L'B'; break;
            case ProductType::TYPE_C: productColor = RGB(147, 51, 234); type = L'C'; break;
            case ProductType::UNKNOWN: productColor = RGB(234, 88, 12); type = L'X'; break;
            }
            RECT productShape{x - 12, beltY - 26, x + 12, beltY - 6};
            HBRUSH productBrush = CreateSolidBrush(productColor);
            FillRect(dc, &productShape, productBrush);
            DeleteObject(productBrush);
            wchar_t label[2]{type, L'\0'};
            SetTextColor(dc, RGB(255, 255, 255));
            DrawTextW(dc, label, -1, &productShape, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    const std::array<LaneId, 4> lanes{LaneId::LANE_1, LaneId::LANE_2,
        LaneId::LANE_3, LaneId::REJECT_LANE};
    const std::array<const wchar_t*, 4> laneLabels{L"LÀN 1", L"LÀN 2", L"LÀN 3", L"LOẠI BỎ"};
    const std::array<COLORREF, 4> laneColors{RGB(37, 99, 235), RGB(22, 163, 74),
        RGB(147, 51, 234), RGB(234, 88, 12)};
    const int routeStart = gateX + 17;
    const int binLeft = bounds.left + 555;
    const int binRight = bounds.right - 8;
    for (int i = 0; i < 4; ++i) {
        const int routeY = bounds.top + 29 + i * 21;
        const bool active = g_diverter.isActive() && selectedLane == lanes[i];
        const COLORREF accent = active ? laneColors[i] : RGB(148, 163, 184);
        HPEN lanePen = CreatePen(PS_SOLID, active ? 4 : 2, accent);
        oldPen = SelectObject(dc, lanePen);
        MoveToEx(dc, routeStart, beltY, nullptr);
        LineTo(dc, routeStart + 24, beltY);
        LineTo(dc, routeStart + 43, routeY + 9);
        LineTo(dc, binLeft - 6, routeY + 9);
        SelectObject(dc, oldPen);
        DeleteObject(lanePen);
        RECT bin{binLeft, routeY, binRight, routeY + 18};
        HBRUSH binBrush = CreateSolidBrush(active ? RGB(219, 234, 254) : RGB(241, 245, 249));
        HPEN binPen = CreatePen(PS_SOLID, 1, active ? laneColors[i] : RGB(203, 213, 225));
        HGDIOBJ previousBrush = SelectObject(dc, binBrush);
        oldPen = SelectObject(dc, binPen);
        RoundRect(dc, bin.left, bin.top, bin.right, bin.bottom, 7, 7);
        SelectObject(dc, oldPen);
        SelectObject(dc, previousBrush);
        DeleteObject(binPen);
        DeleteObject(binBrush);
        SetTextColor(dc, active ? laneColors[i] : RGB(71, 85, 105));
        RECT laneLabel{bin.left + 8, bin.top + 1, bin.right - 5, bin.bottom - 1};
        DrawTextW(dc, laneLabels[i], -1, &laneLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const double currentPosition = g_visualEncoderPosition;
    for (const auto& product : g_divertedVisuals) {
        const double travelled = std::clamp(currentPosition - product.startedAt, 0.0, 20.0);
        const auto laneIt = std::find(lanes.begin(), lanes.end(), product.lane);
        if (laneIt == lanes.end()) continue;
        const int laneIndex = static_cast<int>(std::distance(lanes.begin(), laneIt));
        const int destinationX = binLeft - 16;
        const int destinationY = bounds.top + 29 + laneIndex * 21 + 9;
        const int x = gateX + static_cast<int>((destinationX - gateX) * travelled / 20.0);
        const int y = beltY + static_cast<int>((destinationY - beltY) * travelled / 20.0);
        COLORREF color = laneColors[laneIndex];
        wchar_t letter = L'X';
        switch (product.type) {
        case ProductType::TYPE_A: letter = L'A'; break;
        case ProductType::TYPE_B: letter = L'B'; break;
        case ProductType::TYPE_C: letter = L'C'; break;
        case ProductType::UNKNOWN: letter = L'X'; break;
        }
        RECT shape{x - 9, y - 8, x + 9, y + 8};
        HBRUSH productBrush = CreateSolidBrush(color);
        FillRect(dc, &shape, productBrush);
        DeleteObject(productBrush);
        wchar_t label[2]{letter, L'\0'};
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, label, -1, &shape, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_controller) {
        SetTextColor(dc, RGB(71, 85, 105));
        RECT encoderLabel{bounds.left + 8, bounds.bottom - 19, gateX - 8, bounds.bottom - 2};
        const std::wstring localizedEncoder = L"Encoder: " + std::to_wstring(g_controller->encoderPosition()) +
            L" xung  ·  Đang chờ: " + std::to_wstring(g_controller->pendingProducts().size());
        DrawTextW(dc, localizedEncoder.c_str(), -1, &encoderLabel, DT_LEFT | DT_SINGLELINE);
    }

    if (g_controller && g_controller->pendingProducts().empty()) {
        SetTextColor(dc, RGB(100, 116, 139));
        RECT idleLabel{sensorX + 55, bounds.top + 22, gateX - 25, bounds.top + 40};
        DrawTextW(dc, L"Chưa có sản phẩm trên băng tải", -1, &idleLabel, DT_CENTER | DT_SINGLELINE);
    }
}

void drawButton(const DRAWITEMSTRUCT* item) {
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const int id = static_cast<int>(item->CtlID);
    const COLORREF baseColor = id == ID_STOP ? RGB(220, 38, 38) :
        id == ID_HELP ? RGB(239, 246, 255) : RGB(37, 99, 235);
    const COLORREF color = disabled ? RGB(156, 163, 175) : pressed ? RGB(29, 78, 216) : baseColor;
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
        item->rcItem.right, item->rcItem.bottom, 10, 10);

    wchar_t text[160]{};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, id == ID_HELP ? RGB(29, 78, 216) : RGB(255, 255, 255));
    HGDIOBJ oldFont = SelectObject(item->hDC, g_uiFont);
    RECT bounds = item->rcItem;
    DrawTextW(item->hDC, text, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item->itemState & ODS_FOCUS) != 0) {
        InflateRect(&bounds, -6, -5);
        DrawFocusRect(item->hDC, &bounds);
    }
    SelectObject(item->hDC, oldFont);
    SelectObject(item->hDC, oldPen);
    SelectObject(item->hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        createInterface(window);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED || LOWORD(wParam) == ID_ADD_PRODUCT ||
            LOWORD(wParam) == ID_START || LOWORD(wParam) == ID_STOP ||
            LOWORD(wParam) == ID_BATCH_TOGGLE || LOWORD(wParam) == ID_EXPORT_REPORT ||
            LOWORD(wParam) == ID_HELP)
            handleCommand(LOWORD(wParam));
        return 0;
    case WM_TIMER:
        if (wParam == BATCH_TIMER_ID) runSimulationTimer();
        return 0;
    case WM_DRAWITEM:
        if (lParam != 0) {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID == ID_CONVEYOR_VIEW) drawConveyorView(item);
            else drawButton(item);
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(RGB(244, 247, 252));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        for (const auto& panel : g_panels) {
            HBRUSH fill = CreateSolidBrush(RGB(255, 255, 255));
            HPEN outline = CreatePen(PS_SOLID, 1, RGB(218, 226, 238));
            HGDIOBJ oldBrush = SelectObject(dc, fill);
            HGDIOBJ oldPen = SelectObject(dc, outline);
            RoundRect(dc, panel.bounds.left, panel.bounds.top,
                panel.bounds.right, panel.bounds.bottom, 12, 12);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(outline);
            DeleteObject(fill);

            RECT accent{panel.bounds.left + 1, panel.bounds.top + 1,
                panel.bounds.left + 5, panel.bounds.top + 28};
            HBRUSH accentBrush = CreateSolidBrush(RGB(37, 99, 235));
            FillRect(dc, &accent, accentBrush);
            DeleteObject(accentBrush);
            SetTextColor(dc, RGB(30, 41, 59));
            HGDIOBJ oldFont = SelectObject(dc, g_uiFont);
            RECT title{panel.bounds.left + 14, panel.bounds.top + 4,
                panel.bounds.right - 10, panel.bounds.top + 25};
            DrawTextW(dc, panel.title.c_str(), -1, &title,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldFont);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode(reinterpret_cast<HDC>(wParam), OPAQUE);
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(31, 41, 55));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(window, BATCH_TIMER_ID);
        if (g_controller) g_controller->shutdown();
        if (g_uiFont) DeleteObject(g_uiFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_numberFont) DeleteObject(g_numberFont);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    SetProcessDPIAware();
    g_uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    g_titleFont = CreateFontW(-25, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    g_numberFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    const wchar_t className[] = L"SmartConveyorWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass)) return 1;
    g_window = CreateWindowExW(0, className, L"Băng tải phân loại sản phẩm",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 680, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
