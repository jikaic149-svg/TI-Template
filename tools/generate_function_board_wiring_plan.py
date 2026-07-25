from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path

from openpyxl import Workbook
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.utils import get_column_letter


ROOT = Path(__file__).resolve().parents[1]
PINMUX_JSON = Path(
    r"D:\TI MSPM0\TI ENV\sysconfig_1.27.1\dist\deviceData"
    r"\MSPM0G351X\MSPM0G351X.json"
)
OUTPUT = ROOT / "MSPM0G3519功能底板_小白连连看引脚分配.xlsx"


J1 = {
    1: "PA19", 2: "GND", 3: "PA20", 4: "5V", 5: "PA12", 6: "3V3",
    7: "PA13", 8: "NRST", 9: "PB10", 10: "PA18/BOOT", 11: "PB11",
    12: "PB4", 13: "PB16", 14: "PB5", 15: "PB15", 16: "PB0",
    17: "PB14", 18: "PB1", 19: "PB13", 20: "PA1", 21: "PA7",
    22: "PA0", 23: "PA8", 24: "PA15", 25: "PA9", 26: "PB26",
    27: "PA12", 28: "PB28", 29: "PA13", 30: "PB29", 31: "PA16",
    32: "PB30", 33: "PA22", 34: "PC6", 35: "PB22", 36: "PC7",
}

J2 = {
    1: "PB3", 2: "PA16", 3: "PB2", 4: "PA22", 5: "PC8", 6: "PC2",
    7: "PC9", 8: "PC3", 9: "PB23", 10: "PC4", 11: "PB6", 12: "PC5",
    13: "PB7", 14: "PC0", 15: "PB8", 16: "PC1", 17: "PB9", 18: "PA28",
    19: "PB20", 20: "PA31", 21: "PB24", 22: "PA27", 23: "PB25",
    24: "PA26", 25: "PB27", 26: "PA25", 27: "PA14", 28: "PA24",
    29: "PA17", 30: "PB17", 31: "A3V3", 32: "PB18", 33: "A5V",
    34: "PB19", 35: "GND", 36: "PB21",
}


def carrier_pin(board_pin: int) -> int:
    return board_pin + 1 if board_pin % 2 else board_pin - 1


LOCATIONS: dict[str, tuple[str, int, str, int]] = {}
for board, carrier, mapping in (("J1", "U3", J1), ("J2", "U2", J2)):
    for number, pin in mapping.items():
        LOCATIONS.setdefault(pin, (board, number, carrier, carrier_pin(number)))


def loc(pin: str) -> tuple[str, str]:
    board, number, carrier, carrier_number = LOCATIONS[pin]
    return f"{carrier}-{carrier_number}", f"{board}-{number}"


ASSIGNMENTS = {
    "PWMA": ("PC2", "TIMA0.CCP0"),
    "PWMB": ("PC4", "TIMA0.CCP1"),
    "AIN1": ("PA24", "GPIO"),
    "AIN2": ("PA25", "GPIO"),
    "BIN1": ("PA26", "GPIO"),
    "BIN2": ("PA27", "GPIO"),
    "MOTOR_STBY": ("PB21", "GPIO"),
    "ENC1_A": ("PB29", "TIMG9.CCP0"),
    "ENC1_B": ("PB30", "TIMG9.CCP1"),
    "ENC2_A": ("PB6", "TIMG8.CCP0"),
    "ENC2_B": ("PB7", "TIMG8.CCP1"),
    "BMI_SCLK": ("PB18", "SPI0.SCLK"),
    "BMI_MOSI": ("PB17", "SPI0.PICO"),
    "BMI_MISO": ("PB19", "SPI0.POCI"),
    "BMI_ACC_CS": ("PC0", "GPIO"),
    "BMI_GYR_CS": ("PC1", "GPIO"),
    "BMI_INT1": ("PA28", "GPIO IRQ"),
    "BMI_INT3": ("PA31", "GPIO IRQ"),
    "BT_TX": ("PB10", "UART4.TX"),
    "BT_RX": ("PB11", "UART4.RX"),
    "CAN1_TX": ("PA12", "CANFD0.CANTX"),
    "CAN1_RX": ("PA13", "CANFD0.CANRX"),
    "SERVO_PWM": ("PB13", "TIMG12.CCP0"),
    "BUZZER": ("PA7", "TIMG7.CCP1"),
    "FIVE_V_EN": ("PA15", "GPIO"),
}


def signal_row(group: str, endpoint: str, signal: str, note: str) -> list[str]:
    pin, function = ASSIGNMENTS[signal]
    carrier, board = loc(pin)
    return [group, endpoint, signal, "→", carrier, board, pin, function, note]


ROWS = [
    signal_row("双路电机", "TB6612 T2-8 / PWMA", "PWMA", "电机A调速PWM，建议20 kHz"),
    signal_row("双路电机", "TB6612 T2-2 / PWMB", "PWMB", "电机B调速PWM，与PWMA同一定时器"),
    signal_row("双路电机", "TB6612 T2-6 / AIN1", "AIN1", "电机A方向1"),
    signal_row("双路电机", "TB6612 T2-7 / AIN2", "AIN2", "电机A方向2"),
    signal_row("双路电机", "TB6612 T2-4 / BIN1", "BIN1", "电机B方向1"),
    signal_row("双路电机", "TB6612 T2-3 / BIN2", "BIN2", "电机B方向2"),
    signal_row("双路电机", "TB6612 T2-5 / STBY", "MOTOR_STBY", "加10k下拉，上电默认关闭电机"),
    signal_row("编码器1", "CN3-3 / E1A", "ENC1_A", "电机1编码器A相"),
    signal_row("编码器1", "CN3-4 / E1B", "ENC1_B", "电机1编码器B相"),
    signal_row("编码器2", "CN4-3 / E2A", "ENC2_A", "电机2编码器A相"),
    signal_row("编码器2", "CN4-4 / E2B", "ENC2_B", "电机2编码器B相"),
    ["BMI088", "模块 VCC", "A3V3", "→", "U2-32", "J2-31", "电源", "A3V3", "模块3.3V供电"],
    ["BMI088", "模块 GND", "GND", "→", "U2-36", "J2-35", "电源", "GND", "公共地"],
    signal_row("BMI088", "模块 SCLK", "BMI_SCLK", "SPI时钟"),
    signal_row("BMI088", "模块 MOSI", "BMI_MOSI", "MCU发送数据"),
    signal_row("BMI088", "模块 MISO", "BMI_MISO", "MCU接收数据"),
    signal_row("BMI088", "模块 CSB1", "BMI_ACC_CS", "加速度计片选，低有效"),
    signal_row("BMI088", "模块 CSB2", "BMI_GYR_CS", "陀螺仪片选，低有效"),
    signal_row("BMI088", "模块 INT1", "BMI_INT1", "加速度计数据就绪"),
    signal_row("BMI088", "模块 INT3", "BMI_INT3", "陀螺仪数据就绪"),
    signal_row("蓝牙", "模块 RXD", "BT_TX", "MCU TX接蓝牙RXD"),
    signal_row("蓝牙", "模块 TXD", "BT_RX", "蓝牙TXD接MCU RX"),
    signal_row("CAN1", "CAN收发器 TXD/D", "CAN1_TX", "项目总线CAN1，MCU硬件使用CANFD0"),
    signal_row("CAN1", "CAN收发器 RXD/R", "CAN1_RX", "项目总线CAN1，MCU硬件使用CANFD0"),
    ["CAN1", "CN2-1 与 CN5-1", "CANH", "↔", "CAN收发器 CANH", "—", "—", "CANH", "两个接口完全并联"],
    ["CAN1", "CN2-2 与 CN5-2", "CANL", "↔", "CAN收发器 CANL", "—", "—", "CANL", "两个接口完全并联"],
    signal_row("舵机", "U10-1 / PWM", "SERVO_PWM", "标准舵机控制脉冲，建议50 Hz"),
    ["舵机", "U10-2 / V+", "5V_SW_OUT", "←", "Q2受控5V输出", "—", "电源", "受控5V", "舵机电源"],
    ["舵机", "U10-3 / GND", "GND", "→", "地平面", "J1-2/J2-35", "电源", "GND", "舵机地"],
    signal_row("舵机电源", "Q3驱动输入", "FIVE_V_EN", "控制U10-2的受控5V，默认关闭"),
    signal_row("蜂鸣器", "蜂鸣器驱动输入", "BUZZER", "经MOS/NPN驱动蜂鸣器"),
]


GROUP_COLORS = {
    "双路电机": "DDEBF7",
    "编码器1": "E2F0D9",
    "编码器2": "E2F0D9",
    "BMI088": "FFF2CC",
    "蓝牙": "E4DFEC",
    "CAN1": "D9EAD3",
    "舵机": "FCE5CD",
    "舵机电源": "FCE5CD",
    "蜂鸣器": "D9D2E9",
}


THIN = Side(style="thin", color="808080")


def style(cell, fill: str | None = None, bold: bool = False, size: int = 11) -> None:
    cell.font = Font(name="等线", size=size, bold=bold)
    cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
    cell.border = Border(left=THIN, right=THIN, top=THIN, bottom=THIN)
    if fill:
        cell.fill = PatternFill("solid", fgColor=fill)


def title(ws, text: str, columns: int) -> None:
    ws.merge_cells(start_row=1, start_column=1, end_row=1, end_column=columns)
    cell = ws.cell(1, 1, text)
    cell.font = Font(name="等线", size=16, bold=True)
    cell.alignment = Alignment(horizontal="center", vertical="center")
    cell.fill = PatternFill("solid", fgColor="BDD7EE")
    ws.row_dimensions[1].height = 32


def load_pinmux() -> dict[str, list[str]]:
    data = json.loads(PINMUX_JSON.read_text(encoding="utf-8"))
    package = next(v for v in data["packages"].values() if v["name"] == "LQFP-80(PN)")
    allowed = {entry["devicePinID"] for entry in package["packagePin"]}
    device = {key: value["name"] for key, value in data["devicePins"].items()}
    peripheral = {key: value["name"] for key, value in data["peripheralPins"].items()}
    by_pin: dict[str, list[str]] = defaultdict(list)
    for mux in data["muxes"]:
        if mux["devicePinID"] not in allowed:
            continue
        pin = device[mux["devicePinID"]]
        for setting in mux["muxSetting"]:
            by_pin[pin].append(peripheral[setting["peripheralPinID"]])
    return by_pin


def validate() -> None:
    by_pin = load_pinmux()
    required = {pin: function for pin, function in ASSIGNMENTS.values() if "." in function}
    missing = [(pin, function) for pin, function in required.items() if function not in by_pin[pin]]
    if missing:
        raise RuntimeError(f"PinMux校验失败：{missing}")
    used: dict[str, str] = {}
    for signal, (pin, _) in ASSIGNMENTS.items():
        if pin in used:
            raise RuntimeError(f"引脚重复：{pin} 同时分配给 {used[pin]} 和 {signal}")
        used[pin] = signal


def build() -> Workbook:
    wb = Workbook()
    wb.creator = "Codex"
    wb.title = "MSPM0G3519功能底板小白连连看引脚分配"

    ws = wb.active
    ws.title = "最终连连看"
    title(ws, "模块端 → 功能底板 → 3519最小板 → MCU：照表连线", 9)
    headers = ["功能", "模块这端", "网络名", "方向", "功能板焊盘", "最小板排针", "MCU引脚", "SysConfig/用途", "说明"]
    ws.append(headers)
    for item in ROWS:
        ws.append(item)
    widths = [15, 28, 18, 7, 22, 18, 13, 24, 43]
    for index, width in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(index)].width = width
    for cell in ws[2]:
        style(cell, "BDD7EE", True)
    for row_number in range(3, ws.max_row + 1):
        fill = GROUP_COLORS[str(ws.cell(row_number, 1).value)]
        ws.row_dimensions[row_number].height = 40
        for column in range(1, 10):
            style(ws.cell(row_number, column), fill)
    ws.freeze_panes = "A3"
    ws.auto_filter.ref = f"A2:I{ws.max_row}"
    ws.sheet_view.showGridLines = False
    ws.page_setup.orientation = "landscape"
    ws.page_setup.fitToWidth = 1
    ws.page_setup.fitToHeight = 0
    ws.sheet_properties.pageSetUpPr.fitToPage = True
    ws.print_title_rows = "1:2"

    pinout = wb.create_sheet("接口针序")
    title(pinout, "外部模块接口针序", 6)
    pinout.append(["接口", "针脚", "定义", "连接对象", "电平/电源", "备注"])
    pinout_rows = [
        ["BMI088", 1, "VCC", "A3V3", "3.3V", ""],
        ["BMI088", 2, "GND", "GND", "0V", ""],
        ["BMI088", 3, "SCLK", "PB18/SPI0_SCLK", "3.3V", ""],
        ["BMI088", 4, "MOSI", "PB17/SPI0_PICO", "3.3V", ""],
        ["BMI088", 5, "MISO", "PB19/SPI0_POCI", "3.3V", ""],
        ["BMI088", 6, "CSB1", "PC0/BMI_ACC_CS", "低有效", ""],
        ["BMI088", 7, "CSB2", "PC1/BMI_GYR_CS", "低有效", ""],
        ["BMI088", 8, "INT1", "PA28/BMI_ACC_DRDY", "3.3V", ""],
        ["BMI088", 9, "INT3", "PA31/BMI_GYR_DRDY", "3.3V", ""],
        ["CN2 CAN1", 1, "CANH", "CAN收发器CANH", "差分总线", "与CN5-1并联"],
        ["CN2 CAN1", 2, "CANL", "CAN收发器CANL", "差分总线", "与CN5-2并联"],
        ["CN5 CAN1", 1, "CANH", "CAN收发器CANH", "差分总线", "与CN2-1并联"],
        ["CN5 CAN1", 2, "CANL", "CAN收发器CANL", "差分总线", "与CN2-2并联"],
        ["U10 舵机", 1, "PWM", "PB13/TIMG12_CCP0", "3.3V信号", ""],
        ["U10 舵机", 2, "V+", "5V_SW_OUT", "受控5V", ""],
        ["U10 舵机", 3, "GND", "GND", "0V", ""],
    ]
    for item in pinout_rows:
        pinout.append(item)
    for index, width in enumerate([18, 10, 18, 35, 20, 30], 1):
        pinout.column_dimensions[get_column_letter(index)].width = width
    for cell in pinout[2]:
        style(cell, "BDD7EE", True)
    for row_number in range(3, pinout.max_row + 1):
        group = str(pinout.cell(row_number, 1).value)
        fill = "FFF2CC" if group == "BMI088" else "D9EAD3" if "CAN1" in group else "FCE5CD"
        pinout.row_dimensions[row_number].height = 30
        for column in range(1, 7):
            style(pinout.cell(row_number, column), fill)
    pinout.freeze_panes = "A3"
    pinout.sheet_view.showGridLines = False

    syscfg = wb.create_sheet("SysConfig清单")
    title(syscfg, "MSPM0G3519 SysConfig 最终资源清单", 6)
    syscfg.append(["功能", "实例", "引脚", "建议配置", "上电初始状态", "说明"])
    syscfg_rows = [
        ["电机PWM", "TIMA0", "PC2 CCP0；PC4 CCP1", "Edge aligned；建议20 kHz", "0%", "双通道同步"],
        ["电机方向", "GPIO", "PA24/PA25/PA26/PA27", "推挽输出", "全低", ""],
        ["电机STBY", "GPIO", "PB21", "推挽输出+外部10k下拉", "低", "自检完成后拉高"],
        ["编码器1", "TIMG9", "PB29 CCP0；PB30 CCP1", "Quadrature", "输入", ""],
        ["编码器2", "TIMG8", "PB6 CCP0；PB7 CCP1", "Quadrature", "输入", ""],
        ["BMI088", "SPI0", "PB18 SCLK；PB17 PICO；PB19 POCI", "Mode0；8bit；10 MHz", "空闲", ""],
        ["BMI088片选", "GPIO", "PC0/PC1", "低有效", "输出高", ""],
        ["BMI088中断", "GPIO IRQ", "PA28/PA31", "上升沿", "输入", ""],
        ["蓝牙", "UART4", "PB10 TX；PB11 RX", "8N1", "TX高/RX输入", ""],
        ["CAN1", "CANFD0", "PA12 CANTX；PA13 CANRX", "位速率按总线设置", "正常输入输出", "项目网络名CAN1"],
        ["舵机", "TIMG12", "PB13 CCP0", "50 Hz；脉宽按舵机", "安全中位或关闭", "U10-1"],
        ["舵机5V使能", "GPIO", "PA15", "推挽输出+下拉", "关闭", "控制U10-2"],
        ["蜂鸣器", "TIMG7", "PA7 CCP1", "频率按蜂鸣器", "关闭", "经驱动管"],
        ["板载RGB", "TIMG6", "PA29 CCP0", "保持现有配置", "关闭", "无冲突"],
    ]
    for item in syscfg_rows:
        syscfg.append(item)
    for index, width in enumerate([20, 18, 40, 36, 25, 32], 1):
        syscfg.column_dimensions[get_column_letter(index)].width = width
    for cell in syscfg[2]:
        style(cell, "BDD7EE", True)
    for row_number in range(3, syscfg.max_row + 1):
        syscfg.row_dimensions[row_number].height = 34
        for column in range(1, 7):
            style(syscfg.cell(row_number, column), "E2F0D9")
    syscfg.freeze_panes = "A3"
    syscfg.sheet_view.showGridLines = False
    syscfg.page_setup.orientation = "landscape"
    syscfg.page_setup.fitToWidth = 1
    syscfg.page_setup.fitToHeight = 1
    syscfg.sheet_properties.pageSetUpPr.fitToPage = True

    overview = wb.create_sheet("排针占用总览")
    title(overview, "最小系统板排针占用总览", 7)
    overview.append(["最小板接口", "针号", "功能板接口", "针号", "信号/引脚", "本方案用途", "状态"])
    pin_use = {pin: signal for signal, (pin, _) in ASSIGNMENTS.items()}
    for board, carrier, mapping in (("J1", "U3", J1), ("J2", "U2", J2)):
        for number in range(1, 37):
            pin = mapping[number]
            use = pin_use.get(pin, "")
            state = "已分配" if use else "保留/未使用"
            overview.append([board, number, carrier, carrier_pin(number), pin, use, state])
    for index, width in enumerate([14, 10, 14, 10, 18, 24, 18], 1):
        overview.column_dimensions[get_column_letter(index)].width = width
    for cell in overview[2]:
        style(cell, "BDD7EE", True)
    for row_number in range(3, overview.max_row + 1):
        fill = "E2F0D9" if overview.cell(row_number, 7).value == "已分配" else "FFF2CC"
        overview.row_dimensions[row_number].height = 25
        for column in range(1, 8):
            style(overview.cell(row_number, column), fill)
    overview.freeze_panes = "A3"
    overview.auto_filter.ref = f"A2:G{overview.max_row}"
    overview.sheet_view.showGridLines = False

    for sheet in wb.worksheets:
        sheet.oddHeader.center.text = sheet.title
        sheet.oddFooter.center.text = "第 &P 页，共 &N 页"
    return wb


def main() -> None:
    validate()
    workbook = build()
    workbook.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
