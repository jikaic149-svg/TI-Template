from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path

import fitz
from openpyxl import Workbook
from openpyxl.comments import Comment
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.utils import get_column_letter


ROOT = Path(__file__).resolve().parents[1]
SCHEMATIC = Path(
    r"C:\Users\rlyoulo\Desktop\++圣光机++\电赛\硬件\硬件"
    r"\MCU_MSPM0G3519_V2_0_202601101011_Schematic.pdf"
)
TEMPLATE = Path(
    r"C:\Users\rlyoulo\Desktop\++圣光机++\电赛\硬件\硬件"
    r"\LP-MSPM0G3507引脚功能表.xlsx"
)
SYSCFG = ROOT / "BSP" / "MSPM0G3519" / "MSPM0G3519.syscfg"
RESOURCE_MAP = ROOT / "BSP" / "MSPM0G3519" / "bsp_resource_map.h"
CURRENT_PIN_DOC = ROOT / "docs" / "BOARD_PIN_RESOURCE_RECOMMENDATION.md"
OUTPUT = ROOT / "MSPM0G3519最小系统板引脚功能表.xlsx"


PACKAGE_PIN = {
    "PA0": 1,
    "PA1": 2,
    "PA2": 10,
    "PA3": 11,
    "PA4": 12,
    "PA5": 13,
    "PA6": 14,
    "PA7": 17,
    "PA8": 22,
    "PA9": 23,
    "PA10": 28,
    "PA11": 29,
    "PA12": 41,
    "PA13": 42,
    "PA14": 43,
    "PA15": 44,
    "PA16": 45,
    "PA17": 54,
    "PA18": 55,
    "PA19": 56,
    "PA20": 57,
    "PA21": 61,
    "PA22": 62,
    "PA23": 72,
    "PA24": 73,
    "PA25": 74,
    "PA26": 78,
    "PA27": 79,
    "PA28": 3,
    "PA29": 4,
    "PA30": 5,
    "PA31": 7,
    "PB0": 15,
    "PB1": 16,
    "PB2": 18,
    "PB3": 19,
    "PB4": 20,
    "PB5": 21,
    "PB6": 30,
    "PB7": 31,
    "PB8": 32,
    "PB9": 33,
    "PB10": 34,
    "PB11": 35,
    "PB12": 36,
    "PB13": 37,
    "PB14": 38,
    "PB15": 39,
    "PB16": 40,
    "PB17": 58,
    "PB18": 59,
    "PB19": 60,
    "PB20": 67,
    "PB21": 68,
    "PB22": 69,
    "PB23": 70,
    "PB24": 71,
    "PB25": 75,
    "PB26": 76,
    "PB27": 77,
    "PB28": 24,
    "PB29": 25,
    "PB30": 26,
    "PB31": 27,
    "PC0": 46,
    "PC1": 47,
    "PC2": 50,
    "PC3": 51,
    "PC4": 52,
    "PC5": 53,
    "PC6": 63,
    "PC7": 64,
    "PC8": 65,
    "PC9": 66,
}


J1 = {
    1: "PA19",
    3: "PA20",
    5: "PA12",
    7: "PA13",
    9: "PB10",
    10: "PA18",
    11: "PB11",
    12: "PB4",
    13: "PB16",
    14: "PB5",
    15: "PB15",
    16: "PB0",
    17: "PB14",
    18: "PB1",
    19: "PB13",
    20: "PA1",
    21: "PA7",
    22: "PA0",
    23: "PA8",
    24: "PA15",
    25: "PA9",
    26: "PB26",
    27: "PA12",
    28: "PB28",
    29: "PA13",
    30: "PB29",
    31: "PA16",
    32: "PB30",
    33: "PA22",
    34: "PC6",
    35: "PB22",
    36: "PC7",
}


J2 = {
    1: "PB3",
    2: "PA16",
    3: "PB2",
    4: "PA22",
    5: "PC8",
    6: "PC2",
    7: "PC9",
    8: "PC3",
    9: "PB23",
    10: "PC4",
    11: "PB6",
    12: "PC5",
    13: "PB7",
    14: "PC0",
    15: "PB8",
    16: "PC1",
    17: "PB9",
    18: "PA28",
    19: "PB20",
    20: "PA31",
    21: "PB24",
    22: "PA27",
    23: "PB25",
    24: "PA26",
    25: "PB27",
    26: "PA25",
    27: "PA14",
    28: "PA24",
    29: "PA17",
    30: "PB17",
    32: "PB18",
    34: "PB19",
    36: "PB21",
}


J3 = {3: "PA11", 4: "PA10", 5: "PA20", 6: "PA19"}
J4 = {
    1: "PB6",
    2: "PB7",
    3: "PB8",
    4: "PB9",
    5: "PB20",
    6: "PB24",
    7: "PB25",
    8: "PB27",
}


SPECIAL = {
    "PA0": (
        "3V3 上拉",
        "R16 上拉至 3V3；当前 SysConfig 未配置",
        "√（板载上拉）",
        [],
    ),
    "PA1": (
        "3V3 上拉",
        "R14 上拉至 3V3；当前 SysConfig 未配置",
        "√（板载上拉）",
        [],
    ),
    "PA2": (
        "ROSC 引脚",
        "R17 接 GND；原理图作为 ROSC 偏置使用；当前 SysConfig 未配置",
        "×",
        ["未接排针"],
    ),
    "PA3": (
        "32.768 kHz LFXIN",
        "X1/C4 板载晶振占用；当前 SysConfig 未启用 LFXT",
        "×",
        ["未接排针"],
    ),
    "PA4": (
        "32.768 kHz LFXOUT",
        "X1/C6 板载晶振占用；当前 SysConfig 未启用 LFXT",
        "×",
        ["未接排针"],
    ),
    "PA5": (
        "40 MHz HFXIN",
        "X2/C8；SysConfig 已配置 40 MHz HFXT 输入",
        "×",
        ["未接排针"],
    ),
    "PA6": (
        "40 MHz HFXOUT",
        "X2/C9；SysConfig 已配置 40 MHz HFXT 输出",
        "×",
        ["未接排针"],
    ),
    "PA10": (
        "UART0_TX（调试串口）",
        "连接 CH340X 和 DAP-Link；当前 SysConfig 未配置 UART0",
        "谨慎使用",
        ["CH340X", "DAP-Link"],
    ),
    "PA11": (
        "UART0_RX（调试串口）",
        "连接 CH340X 和 DAP-Link；当前 SysConfig 未配置 UART0",
        "谨慎使用",
        ["CH340X", "DAP-Link"],
    ),
    "PA14": (
        "板载 LED1",
        "LED1/R9 接 3V3，低电平点亮；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        ["LED1"],
    ),
    "PA17": (
        "板载 LED2",
        "LED2/R10 接 3V3，低电平点亮；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        ["LED2"],
    ),
    "PA18": (
        "BOOT/BSL 按键",
        "K3 接 3V3、R15 下拉；启动模式相关；当前 SysConfig 未配置 GPIO",
        "不建议使用",
        ["K3"],
    ),
    "PA19": (
        "SWDIO",
        "连接 DAP-Link；SysConfig Board 已配置 SWDIO",
        "×",
        ["DAP-Link"],
    ),
    "PA20": (
        "SWCLK",
        "连接 DAP-Link；SysConfig Board 已配置 SWCLK",
        "×",
        ["DAP-Link"],
    ),
    "PA21": (
        "VREF−",
        "原理图直接连接 GND",
        "×",
        ["未接排针"],
    ),
    "PA23": (
        "VREF+",
        "原理图连接 A3V3 模拟电源",
        "×",
        ["未接排针"],
    ),
    "PA29": (
        "SK6812 RGB 数据",
        "LED3 DIN；SysConfig 已配置 TIMG6_CCP0（RGB_PWM）",
        "×（SysConfig 占用）",
        ["LED3-2", "未接排针"],
    ),
    "PA30": (
        "OLED/TFT 背光",
        "U5-8 TFT_BLK；当前 SysConfig 未配置",
        "谨慎使用",
        ["U5-8", "未接排针"],
    ),
    "PB2": (
        "OLED/TFT MOSI",
        "U5-4 OLED_MOSI；当前 SysConfig 未配置 SPI0",
        "谨慎使用",
        ["U5-4"],
    ),
    "PB3": (
        "OLED/TFT CLK",
        "U5-3 OLED_CLK；当前 SysConfig 未配置 SPI0",
        "谨慎使用",
        ["U5-3"],
    ),
    "PB6": (
        "4×4 键盘 H1",
        "矩阵键盘行 H1；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB7": (
        "4×4 键盘 H2",
        "矩阵键盘行 H2；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB8": (
        "4×4 键盘 H3",
        "矩阵键盘行 H3；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB9": (
        "4×4 键盘 H4",
        "矩阵键盘行 H4；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB12": (
        "ICM-45686 CS",
        "U10-12 IMU_CS；当前 SysConfig 未配置 SPI1/GPIO",
        "谨慎使用",
        ["U10-12", "未接排针"],
    ),
    "PB14": (
        "ICM-45686 MISO",
        "U10-1 IMU_MISO；当前 SysConfig 未配置 SPI1",
        "谨慎使用",
        ["U10-1"],
    ),
    "PB15": (
        "ICM-45686 MOSI",
        "U10-14 IMU_MOSI；当前 SysConfig 未配置 SPI1",
        "谨慎使用",
        ["U10-14"],
    ),
    "PB16": (
        "ICM-45686 CLK",
        "U10-13 IMU_CLK；当前 SysConfig 未配置 SPI1",
        "谨慎使用",
        ["U10-13"],
    ),
    "PB20": (
        "4×4 键盘 V1",
        "矩阵键盘列 V1；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB22": (
        "ICM-45686 INT1",
        "U10-4 IMU_INT1；当前 SysConfig 未配置 GPIO/IRQ",
        "谨慎使用",
        ["U10-4"],
    ),
    "PB23": (
        "OLED/TFT RESET",
        "U5-5 OLED_RES；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        ["U5-5"],
    ),
    "PB24": (
        "4×4 键盘 V2",
        "矩阵键盘列 V2；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB25": (
        "4×4 键盘 V3",
        "矩阵键盘列 V3；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB27": (
        "4×4 键盘 V4",
        "矩阵键盘列 V4；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        [],
    ),
    "PB28": (
        "ICM-45686 INT2",
        "U10-9 IMU_INT2；当前 SysConfig 未配置 GPIO/IRQ",
        "谨慎使用",
        ["U10-9"],
    ),
    "PB31": (
        "USER 按键",
        "K2 按下接 GND；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        ["K2", "未接排针"],
    ),
    "PC8": (
        "OLED/TFT DC",
        "U5-6 OLED_DC；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        ["U5-6"],
    ),
    "PC9": (
        "OLED/TFT CS",
        "U5-7 OLED_CS；当前 SysConfig 未配置 GPIO",
        "谨慎使用",
        ["U5-7"],
    ),
}


def read_sources() -> tuple[str, str, str, str]:
    for source in (SCHEMATIC, TEMPLATE, SYSCFG, RESOURCE_MAP, CURRENT_PIN_DOC):
        if not source.exists():
            raise FileNotFoundError(source)

    syscfg_text = SYSCFG.read_text(encoding="utf-8")
    resource_text = RESOURCE_MAP.read_text(encoding="utf-8")
    doc_text = CURRENT_PIN_DOC.read_text(encoding="utf-8")

    required_syscfg = (
        'hfCrystal.inputFreq                    = 40;',
        'hfCrystal.peripheral.hfxInPin.$assign  = "PA5";',
        'hfCrystal.peripheral.hfxOutPin.$assign = "PA6";',
        'RGBPWM.peripheral.$assign                 = "TIMG6";',
        'RGBPWM.peripheral.ccp0Pin.$assign         = "PA29";',
        'Board.peripheral.swclkPin.$assign = "PA20";',
        'Board.peripheral.swdioPin.$assign = "PA19";',
    )
    missing = [token for token in required_syscfg if token not in syscfg_text]
    if missing:
        raise RuntimeError(f"SysConfig 关键配置与生成器预期不符：{missing}")

    with fitz.open(SCHEMATIC) as pdf:
        if pdf.page_count != 1:
            raise RuntimeError(f"原理图页数已变化：{pdf.page_count}")
        schematic_text = pdf[0].get_text("text")
    required_schematic = (
        "MSPM0G3519SPNR",
        "ICM-45686",
        "OLED_CLK",
        "IMU_INT1",
        "IMU_INT2",
        "PB25",
        "PA17",
    )
    missing = [token for token in required_schematic if token not in schematic_text]
    if missing:
        raise RuntimeError(f"原理图关键网络未找到：{missing}")

    return syscfg_text, resource_text, doc_text, schematic_text


def extract_doc_value(doc_text: str, label: str) -> str:
    match = re.search(rf"^\|\s*{re.escape(label)}\s*\|\s*([^|]+?)\s*\|", doc_text, re.MULTILINE)
    return match.group(1).strip() if match else "未找到"


def make_locations() -> dict[str, list[str]]:
    locations: dict[str, list[str]] = defaultdict(list)
    for connector, mapping in (("J1", J1), ("J2", J2), ("J3", J3), ("J4", J4)):
        for number, pin in mapping.items():
            locations[pin].append(f"{connector}-{number}")
    return locations


def pin_rows(locations: dict[str, list[str]]) -> list[list[str]]:
    pins = (
        [f"PA{i}" for i in range(32)]
        + [f"PB{i}" for i in range(32)]
        + [f"PC{i}" for i in range(10)]
    )
    rows: list[list[str]] = []
    for pin in pins:
        function = "无（通用 GPIO）"
        connection = "无板载专用连接；当前 SysConfig 未配置"
        usable = "√"
        extra_locations: list[str] = []
        if pin in SPECIAL:
            function, connection, usable, extra_locations = SPECIAL[pin]

        all_locations = [*locations.get(pin, []), *extra_locations]
        unique_locations = list(dict.fromkeys(all_locations))
        if not unique_locations:
            unique_locations = ["未接排针"]
        rows.append(
            [pin, function, connection, usable, "；".join(unique_locations)]
        )
    return rows


def apply_cell_style(cell, *, bold: bool = False, fill: str | None = None) -> None:
    cell.font = Font(name="等线", size=11, bold=bold, color="000000")
    cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
    cell.border = Border(
        left=Side(style="thin", color="000000"),
        right=Side(style="thin", color="000000"),
        top=Side(style="thin", color="000000"),
        bottom=Side(style="thin", color="000000"),
    )
    if fill:
        cell.fill = PatternFill("solid", fgColor=fill)


def build_workbook(doc_text: str, resource_text: str) -> Workbook:
    wb = Workbook()
    wb.creator = "Codex"
    wb.title = "MSPM0G3519 最小系统板引脚功能表"
    wb.subject = "原理图、SysConfig 与当前项目引脚文档交叉校验"
    wb.description = (
        "依据启是科技 MSPM0G3519 V2.0 原理图和当前工程 MSPM0G3519.syscfg 生成"
    )

    ws = wb.active
    ws.title = "引脚功能表"
    headers = ["引脚", "最小系统板功能", "相关连接 / SysConfig", "是否可以使用", "引出位置"]
    ws.append(headers)
    for row in pin_rows(make_locations()):
        ws.append(row)

    widths = [9, 27, 52, 19, 30]
    for index, width in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(index)].width = width
    ws.row_dimensions[1].height = 25
    for row in range(2, ws.max_row + 1):
        ws.row_dimensions[row].height = 34

    for cell in ws[1]:
        apply_cell_style(cell, bold=True, fill="FFFFFF")
    for row in range(2, ws.max_row + 1):
        status = str(ws.cell(row, 4).value)
        if status.startswith("×"):
            fill = "FCE4D6"
        elif status == "不建议使用":
            fill = "FCE4D6"
        elif status == "谨慎使用":
            fill = "FFE699"
        else:
            fill = "FFF2CC"
        for col in range(1, 6):
            apply_cell_style(ws.cell(row, col), fill=fill)
        pin = str(ws.cell(row, 1).value)
        ws.cell(row, 1).comment = Comment(
            f"U8（LQFP-80/PN）封装脚号：{PACKAGE_PIN[pin]}", "Codex"
        )

    ws.freeze_panes = "A2"
    ws.auto_filter.ref = f"A1:E{ws.max_row}"
    ws.sheet_view.showGridLines = False
    ws.page_setup.orientation = "landscape"
    ws.page_setup.fitToWidth = 1
    ws.page_setup.fitToHeight = 0
    ws.sheet_properties.pageSetUpPr.fitToPage = True
    ws.print_title_rows = "1:1"
    ws.print_area = f"A1:E{ws.max_row}"
    ws.oddHeader.center.text = "MSPM0G3519 最小系统板引脚功能表"
    ws.oddFooter.center.text = "第 &P 页，共 &N 页"

    check = wb.create_sheet("校验说明")
    check.merge_cells("A1:D1")
    check["A1"] = "MSPM0G3519 V2.0 引脚交叉校验说明"
    check["A1"].font = Font(name="等线", size=15, bold=True)
    check["A1"].alignment = Alignment(horizontal="center", vertical="center")
    check["A1"].fill = PatternFill("solid", fgColor="D9EAF7")
    check.row_dimensions[1].height = 30

    check.append(["资料", "位置/版本", "用途", "校验结果"])
    source_rows = [
        ["原理图", str(SCHEMATIC), "板载连接、排针位置、封装脚号", "MSPM0G3519SPNR，V2.0"],
        ["SysConfig", str(SYSCFG.relative_to(ROOT)), "当前实际配置", "HFXT、SWD、PA29 RGB PWM"],
        ["资源映射", str(RESOURCE_MAP.relative_to(ROOT)), "BSP 当前已实现资源", "除板载 RGB 外，业务资源均未绑定"],
        ["当前引脚文档", str(CURRENT_PIN_DOC.relative_to(ROOT)), "交叉校验", "发现两处管脚差异"],
        ["参考表格", str(TEMPLATE), "列结构与版式参考", "已按同类五列表格生成"],
    ]
    for row in source_rows:
        check.append(row)

    start = check.max_row + 2
    check.cell(start, 1, "校验项")
    check.cell(start, 2, "原理图 / SysConfig")
    check.cell(start, 3, "当前文档或模板")
    check.cell(start, 4, "结论")

    doc_led = extract_doc_value(doc_text, "LED1/LED2")
    doc_keyboard = extract_doc_value(doc_text, "4×4 键盘列")
    resource_disabled = all(
        re.search(rf"^#define\s+{name}\s+0\s*$", resource_text, re.MULTILINE)
        for name in (
            "BSP_RESOURCE_HAS_PWM",
            "BSP_RESOURCE_HAS_UART",
            "BSP_RESOURCE_HAS_CAN",
        )
    )
    validation_rows = [
        ["40 MHz HFXT", "PA5/PA6，SysConfig 已配置", "当前文档 PA5/PA6", "一致"],
        ["SWD", "PA19 SWDIO、PA20 SWCLK，SysConfig 已配置", "当前文档 PA19/PA20", "一致"],
        ["SK6812 RGB", "PA29 / TIMG6_CCP0，SysConfig 已配置", "当前文档 PA29", "一致"],
        ["32.768 kHz LFXT", "PA3/PA4 板载占用，SysConfig 未启用", "当前文档 PA3/PA4", "物理占用，不能按空闲 GPIO 处理"],
        ["LED1/LED2", "PA14 / PA17", doc_led, "不一致：LED2 应为 PA17，PA16 为通用引脚"],
        ["4×4 键盘列", "PB20 / PB24 / PB25 / PB27", doc_keyboard, "不一致：PB21 应改为 PB25；PB21 引至 J2-36"],
        ["板载 ICM-45686", "PB12/PB14/PB15/PB16/PB22/PB28", "当前文档相同", "一致，但当前 SysConfig 尚未配置"],
        ["OLED/TFT", "PB2/PB3/PB23/PC8/PC9/PA30", "当前文档相同", "一致，但当前 SysConfig 尚未配置"],
        ["调试 UART", "PA10 TX / PA11 RX", "当前文档相同", "原理图已连接，当前 SysConfig 尚未配置 UART0"],
        ["BSP 业务资源", "PWM/UART/CAN 等 HAS 宏为 0", "当前项目资源表", "一致" if resource_disabled else "需要重新核对"],
        ["表格覆盖范围", "PA0–PA31、PB0–PB31、PC0–PC9，共 74 个 GPIO", "G3507 模板止于 PB27", "已补充 PB28–PB31 与 PC0–PC9"],
    ]
    for row in validation_rows:
        check.append(row)

    check_widths = [24, 55, 45, 48]
    for index, width in enumerate(check_widths, 1):
        check.column_dimensions[get_column_letter(index)].width = width
    for row in range(2, check.max_row + 1):
        check.row_dimensions[row].height = 36
        for col in range(1, 5):
            fill = None
            if row == 2 or row == start:
                fill = "D9EAF7"
            apply_cell_style(check.cell(row, col), bold=(row == 2 or row == start), fill=fill)

    for row in range(start + 1, check.max_row + 1):
        conclusion = str(check.cell(row, 4).value)
        if conclusion.startswith("不一致"):
            for col in range(1, 5):
                check.cell(row, col).fill = PatternFill("solid", fgColor="FCE4D6")
        elif conclusion == "一致" or conclusion.startswith("一致，但"):
            for col in range(1, 5):
                check.cell(row, col).fill = PatternFill("solid", fgColor="E2F0D9")

    check.freeze_panes = "A3"
    check.sheet_view.showGridLines = False
    check.page_setup.orientation = "landscape"
    check.page_setup.fitToWidth = 1
    check.page_setup.fitToHeight = 1
    check.sheet_properties.pageSetUpPr.fitToPage = True
    check.print_area = f"A1:D{check.max_row}"

    return wb


def main() -> None:
    _, resource_text, doc_text, _ = read_sources()
    workbook = build_workbook(doc_text, resource_text)
    workbook.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
