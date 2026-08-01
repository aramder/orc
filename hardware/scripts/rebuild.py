import sys, json, math
sys.path.insert(0, r"C:\Users\aramder\Documents\GitHub\KiCAD-MCP-Server\python")
from kicad_interface import KiCADInterface

PROJECT_DIR = r"C:\Users\aramder\Documents\GitHub\orc\hardware"
SCH = PROJECT_DIR + r"\orc.kicad_sch"
PRO = PROJECT_DIR + r"\orc.kicad_pro"

kc = KiCADInterface()
FAILS = []


def cmd(name, params=None):
    r = kc.handle_command(name, params or {})
    ok = r.get("success", True) if isinstance(r, dict) else True
    if not ok:
        FAILS.append((name, params, r))
        print(f"[FAIL] {name} {params.get('reference') or params.get('componentRef')} : {r.get('message')}")
    return r


def place(ref, library, comp_type, x, y, value=None, unit=1):
    return cmd(
        "add_schematic_component",
        {"schematicPath": SCH, "component": {
            "type": comp_type, "library": library, "reference": ref,
            "value": value or comp_type, "x": x, "y": y, "unit": unit}},
    )


PIN_CACHE = {}


def get_pins(ref):
    if ref not in PIN_CACHE:
        r = cmd("get_schematic_pin_locations", {"schematicPath": SCH, "reference": ref})
        PIN_CACHE[ref] = r.get("pins", {}) if isinstance(r, dict) else {}
    return PIN_CACHE[ref]


def net_long(ref, pin_num, net_name, stub=12.7):
    """Long stub for dense ICs: manual wire + label placed well clear of the
    symbol's own pin-name text, instead of connect_to_net's tight 2.54mm stub."""
    pins = get_pins(ref)
    p = pins.get(pin_num)
    if not p:
        FAILS.append(("net_long", (ref, pin_num, net_name), "pin not found"))
        print(f"[FAIL] net_long {ref}/{pin_num} -> {net_name}: pin not found")
        return
    x, y, angle = p["x"], p["y"], p["angle"]
    dx = {0: 1, 90: 0, 180: -1, 270: 0}[int(angle) % 360]
    dy = {0: 0, 90: -1, 180: 0, 270: 1}[int(angle) % 360]
    ex, ey = round(x + dx * stub, 2), round(y + dy * stub, 2)
    cmd("add_schematic_wire", {"schematicPath": SCH, "waypoints": [[x, y], [ex, ey]], "snapToPins": False})
    cmd("add_schematic_net_label", {"schematicPath": SCH, "netName": net_name, "position": [ex, ey]})


def net(ref, pin_num, net_name):
    """All passives now also use the manual long-stub path (connect_to_net's
    internal PinLocator cache goes stale after ~15-20 calls in one process)."""
    return net_long(ref, pin_num, net_name, stub=7.62)


def find_pin(ref, *name_subs):
    """Find a pin number on an already-placed component by name substring."""
    for num, info in get_pins(ref).items():
        nm = str(info.get("name", "")).upper()
        for sub in name_subs:
            if sub.upper() == nm or sub.upper() in nm:
                return num
    return None


def net_by_name(ref, net_name, *name_subs, stub=10.16):
    pin_num = find_pin(ref, *name_subs)
    if pin_num is None:
        FAILS.append(("net_by_name", (ref, name_subs, net_name), "pin not found"))
        print(f"[FAIL] net_by_name {ref}/{name_subs} -> {net_name}: pin not found")
        return
    net_long(ref, pin_num, net_name, stub=stub)


# ---------------------------------------------------------------------------
kc.handle_command("open_project", {"filename": PRO})
with open(SCH, "r", encoding="utf-8") as f:
    content = f.read()
content = content.replace('(paper "A4")', '(paper "A0")')
with open(SCH, "w", encoding="utf-8", newline="\n") as f:
    f.write(content)

print("=== PHASE A: Domain A ===")
# USB-C -- dense 16-pin connector, long stubs
place("J1", "Connector", "USB_C_Receptacle_USB2.0_16P", 20, 40)
    # VBUS pins go straight onto VIN_BUCK_A -- no diode-OR (see source
    # arbitration note below), so USB VBUS and the buck's Vin are literally
    # the same node now, not two nets joined by a component.
for pin, name in [("A4", "VIN_BUCK_A"), ("B4", "VIN_BUCK_A"), ("A9", "VIN_BUCK_A"), ("B9", "VIN_BUCK_A"),
                   ("A1", "GND_A"), ("B1", "GND_A"), ("A12", "GND_A"), ("B12", "GND_A"),
                   ("A6", "USB_DP"), ("B6", "USB_DP"), ("A7", "USB_DM"), ("B7", "USB_DM")]:
    net_long("J1", pin, name, stub=15.24)

# Combined DC+CAN terminal -- circuit-draft.md: dedicated hardwired terminal,
# decoupled from the harness A+ tap (sharing that tap would re-connect the two
# "isolated" domains through a common supply return, defeating ADuM1250).
# DORABO DB128L-5.08-4P-GN-S, LCSC C2827883, right-angle screw terminal, 4-pos.
place("J5", "Connector_Generic", "Conn_01x04", 40, 130, value="DC+CAN terminal (C2827883)")
net_long("J5", "1", "VEH_RAIL_A_IN", stub=10.16)
net_long("J5", "2", "GND_A", stub=10.16)
net_long("J5", "3", "CAN_H", stub=10.16)
net_long("J5", "4", "CAN_L", stub=10.16)

# PTC resettable fuse, DC-terminal input -- circuit-draft.md BOM #4: Littelfuse
# 2920L075/60MR (C207083), 0.75A hold/1.5A trip/60V, shared part with F2 below.
# Was flagged in the draft but never actually placed on the schematic -- fixing
# that here rather than leaving it as sourcing-only paperwork.
place("F1", "Device", "Fuse", 55, 105, value="0.75A PTC (C207083)")
net("F1", "1", "VEH_RAIL_A_IN")
net("F1", "2", "VEH_RAIL_A")

place("Q1", "Transistor_FET", "AO3401A", 70, 120, value="AO3401A")
net("Q1", "2", "VEH_RAIL_A")
net("Q1", "3", "V_PROT_A")
net("Q1", "1", "Q1_GATE")
place("R1", "Device", "R", 70, 140, value="10k")
net("R1", "1", "Q1_GATE")
net("R1", "2", "GND_A")

# Source arbitration -- circuit-draft.md decision: active P-FET source-select,
# NOT diode-OR (diode-OR was evaluated and rejected on hard numbers: USB-IF's
# 4.4V worst-case VBUS floor is already below the buck's 4.5V min input with
# ZERO diode drop added). USB_VBUS goes straight to VIN_BUCK_A -- no diode.
# V_PROT_A (DC terminal, post reverse-polarity) goes through a P-FET that's
# gated OFF whenever USB VBUS is present, so only one source is ever
# electrically connected to Vin at a time.
place("Q3", "Transistor_FET", "AO3401A", 110, 30, value="AO3401A")
net_long("Q3", "2", "V_PROT_A", stub=12.7)
net_long("Q3", "3", "VIN_BUCK_A", stub=10.16)
net_long("Q3", "1", "Q3_GATE", stub=12.7)
place("R14", "Device", "R", 110, 55, value="10k")
net("R14", "1", "Q3_GATE")
net("R14", "2", "GND_A")

# USB-presence detect driving Q3's gate -- FIXED this pass. The prior single-
# NPN version was electrically broken: an NPN pulling its collector toward
# GND can only pull Q3's gate DOWN, which is the "turn ON" direction for a
# P-FET, not "turn off" -- it could never disable the DC path when VBUS
# appears. Real fix needs a second, high-side stage that can pull the gate UP
# to Q3's source potential (V_PROT_A, which floats up to ~28.8V, not a fixed
# logic rail):
#   Q4 (NPN, low-side, VBUS-driven) -- base from VIN_BUCK_A (=USB VBUS on this
#     node, see the diode-OR-removal note above) through R15; when VBUS is
#     present Q4 saturates and pulls its collector (Q4_COL) toward GND_A.
#   R17 -- pull-up from Q4_COL to V_PROT_A (Q3's source rail), so with VBUS
#     absent (Q4 off) Q4_COL sits near V_PROT_A -- i.e. near Q5's emitter --
#     keeping Q5 off and leaving Q3 gated by R14's pull-down alone (default:
#     DC path ON).
#   R18 -- base resistor from Q4_COL into Q5's base.
#   Q5 (PNP, high-side) -- emitter on V_PROT_A, base driven via R18/R17 off
#     Q4's collector, collector on Q3_GATE. When VBUS appears, Q4_COL gets
#     pulled toward GND_A, Q5's base-emitter goes strongly forward-biased
#     (Veb ~ V_PROT_A), Q5 saturates and pulls Q3's gate up to ~V_PROT_A ->
#     Vgs(Q3) ~ 0 -> Q3 turns OFF, disconnecting the DC-terminal path.
# R17 sized for a few mA at V_PROT_A's ~28.8V worst case (28.8V/22k ~ 1.3mA,
# ~37mW continuous dissipation while VBUS is present -- acceptable, not
# speed-critical). R15/R18 generic 4.7-10k base resistors, not yet pulled
# from a live catalog (see BOM.md).
place("Q4", "Transistor_BJT", "MMBT2222A", 110, 100, value="MMBT2222A")
net("Q4", "2", "GND_A")
net("Q4", "3", "Q4_COL")
place("R15", "Device", "R", 140, 100, value="10k")
net("R15", "1", "VIN_BUCK_A")
net("R15", "2", "Q4_BASE")
net("Q4", "1", "Q4_BASE")

place("R17", "Device", "R", 200, 90, value="22k")
net("R17", "1", "V_PROT_A")
net("R17", "2", "Q4_COL")
place("R18", "Device", "R", 200, 120, value="4.7k")
net("R18", "1", "Q4_COL")
net("R18", "2", "Q5_BASE")
place("Q5", "Transistor_BJT", "MMBT3906", 200, 150, value="MMBT3906")
net("Q5", "1", "Q5_BASE")
net("Q5", "2", "V_PROT_A")
net_long("Q5", "3", "Q3_GATE", stub=12.7)

place("U3", "Regulator_Switching", "LM2596S-ADJ", 170, 60, value="LM2596S-ADJ")
net_long("U3", "1", "VIN_BUCK_A", stub=10.16)
net_long("U3", "5", "VIN_BUCK_A", stub=10.16)
net_long("U3", "3", "GND_A", stub=10.16)
net_long("U3", "2", "SW_A", stub=10.16)
net_long("U3", "4", "FB_A", stub=10.16)

place("L1", "Device", "L", 230, 45, value="68uH")
net("L1", "1", "SW_A")
net("L1", "2", "3V3_A")
place("D3", "Device", "D", 230, 75, value="SS56")
net("D3", "1", "GND_A")
net("D3", "2", "SW_A")

# CIN realized as 2x47uF polymer in parallel (C1/C1B) rather than one bulk
# cap -- matches the sourced part (Nichicon PCR1J470MCL1GS, C3274436) and the
# RMS-ripple-current-based right-sizing in circuit-draft.md, not a single
# oversized/undersized generic electrolytic.
place("C1", "Device", "C_Polarized", 195, 90, value="47uF")
net("C1", "1", "VIN_BUCK_A")
net("C1", "2", "GND_A")
place("C1B", "Device", "C_Polarized", 195, 115, value="47uF")
net("C1B", "1", "VIN_BUCK_A")
net("C1B", "2", "GND_A")
# CIN HF bypass, right at the IC pin -- does not replace the bulk polymer
# pair above (TI warns ceramic-only Vin bypassing can ring on this part).
place("C19", "Device", "C", 195, 140, value="1uF/100V")
net("C19", "1", "VIN_BUCK_A")
net("C19", "2", "GND_A")

place("C2", "Device", "C_Polarized", 260, 40, value="220uF")
net("C2", "1", "3V3_A")
net("C2", "2", "GND_A")
place("C3", "Device", "C", 285, 40, value="1uF")
net("C3", "1", "3V3_A")
net("C3", "2", "GND_A")
place("R2", "Device", "R", 320, 40, value="2.0k")
net("R2", "1", "3V3_A")
net_long("R2", "2", "FB_A", stub=10.16)
place("R3", "Device", "R", 320, 90, value="1.2k")
net_long("R3", "1", "FB_A", stub=10.16)
net("R3", "2", "GND_A")

# ESP32-S3-WROOM-1U -- the MCU itself (was dropped in the rebuild by mistake)
# NOTE: real part is -1U (ext. antenna), footprint must be swapped before
# layout -- symbol here is the pin-compatible generic -1 (onboard antenna).
place("U1", "RF_Module", "ESP32-S3-WROOM-1", 70, 220, value="ESP32-S3-WROOM-1U")
net_long("U1", "2", "3V3_A", stub=12.7)
net_long("U1", "1", "GND_A", stub=12.7)
net_long("U1", "40", "GND_A", stub=12.7)
net_long("U1", "41", "GND_A", stub=12.7)
net_long("U1", "3", "EN_A", stub=12.7)
net_long("U1", "13", "USB_DM", stub=12.7)
net_long("U1", "14", "USB_DP", stub=12.7)
net_long("U1", "12", "SDA_A", stub=12.7)
net_long("U1", "17", "SCL_A", stub=12.7)
net_long("U1", "27", "GPIO0_A", stub=12.7)
net_long("U1", "16", "GPIO46_A", stub=12.7)

place("C4", "Device", "C", 30, 200, value="10uF")
net("C4", "1", "3V3_A")
net("C4", "2", "GND_A")
place("C5", "Device", "C", 30, 225, value="1uF")
net("C5", "1", "3V3_A")
net("C5", "2", "GND_A")
place("C6", "Device", "C", 30, 280, value="0.1uF")
net("C6", "1", "3V3_A")
net("C6", "2", "GND_A")
place("C7", "Device", "C", 30, 305, value="0.1uF")
net("C7", "1", "3V3_A")
net("C7", "2", "GND_A")

place("R4", "Device", "R", 150, 200, value="10k")
net("R4", "1", "3V3_A")
net("R4", "2", "EN_A")
place("C8", "Device", "C", 150, 225, value="1uF")
net("C8", "1", "EN_A")
net("C8", "2", "GND_A")

place("R5", "Device", "R", 150, 280, value="10k")
net("R5", "1", "3V3_A")
net("R5", "2", "GPIO0_A")
place("R6", "Device", "R", 150, 305, value="10k")
net("R6", "1", "GPIO46_A")
net("R6", "2", "GND_A")

# CAN interface -- added back per circuit-draft.md, provisional
# ("descope if it doesn't comfortably fit the board" -- user's own caveat).
# LCSC/description detail lives in BOM.md, not crammed onto value fields here
# -- that's what caused the illegible pile-up on the first pass at this block.
place("U7", "Interface_CAN_LIN", "SN65HVD230", 600, 60, value="SN65HVD230")
place("C17", "Device", "C", 560, 30, value="100nF")
net("C17", "1", "3V3_A")
net("C17", "2", "GND_A")

place("R16", "Device", "R", 700, 30, value="120R")
place("J6", "Connector_Generic", "Conn_01x02", 760, 30, value="Term. jumper")

place("U8", "Power_Protection", "USBLC6-2SC6", 600, 180, value="USBLC6-2SC6")

place("D6", "Device", "D_TVS_Dual_AAC", 720, 180, value="D_TVS_Dual (placeholder)")

r = cmd("save_project", {})
print("save A (pre-CAN-wiring):", r.get("success"))

# Wire U7/U8 by actual pin name (fetched after placement -- SOIC-8/SOT-23-6
# pin numbering not assumed from memory)
net_by_name("U7", "GND_A", "GND")
net_by_name("U7", "3V3_A", "VCC")
net_by_name("U7", "CAN_H", "CANH")
net_by_name("U7", "CAN_L", "CANL")
net_by_name("U7", "GND_A", "RS")   # Rs tied low -> high-speed mode (typical app)
# U1 TXD0 (MCU transmit) -> U7 RXD (transceiver's input from MCU)
# U1 RXD0 (MCU receive)  -> U7 TXD (transceiver's output to MCU)
net_by_name("U1", "UART_TX_A", "TXD0")
net_by_name("U1", "UART_RX_A", "RXD0")
net_by_name("U7", "UART_TX_A", "D")   # SN65HVD230 pin1 "D" = TXD, driver input from MCU
net_by_name("U7", "UART_RX_A", "R")   # pin4 "R" = RXD, receiver output to MCU

net("R16", "1", "CAN_H")
net("R16", "2", "TERM_MID")
net("J6", "1", "TERM_MID")
net("J6", "2", "CAN_L")

net_by_name("U8", "VIN_BUCK_A", "VBUS")
net_by_name("U8", "GND_A", "GND")
net_by_name("U8", "USB_DP", "I/O1")
net_by_name("U8", "USB_DM", "I/O2")

net("D6", "1", "CAN_H")
net("D6", "2", "CAN_L")
net("D6", "3", "GND_A")  # AAC variant: pin 3 is the shared/common center pin

r = cmd("save_project", {})
print("save A:", r.get("success"))

print("=== PHASE B: Barrier (ADuM1250) ===")
BX = 400
place("U2", "Isolator", "ADuM1250", BX + 20, 60, value="ADuM1250ARZ-RL7")
net_long("U2", "1", "3V3_A", stub=10.16)
net_long("U2", "4", "GND_A", stub=10.16)
net_long("U2", "2", "SDA_A", stub=10.16)
net_long("U2", "3", "SCL_A", stub=10.16)
net_long("U2", "5", "GND_B", stub=10.16)
net_long("U2", "8", "3V3_B", stub=10.16)
net_long("U2", "7", "SDA_B", stub=10.16)
net_long("U2", "6", "SCL_B", stub=10.16)

place("C9", "Device", "C", BX - 20, 40, value="0.1uF")
net("C9", "1", "3V3_A")
net("C9", "2", "GND_A")
place("C10", "Device", "C", BX + 60, 40, value="0.1uF")
net("C10", "1", "3V3_B")
net("C10", "2", "GND_B")

place("R7", "Device", "R", BX - 30, 90, value="10k")
net("R7", "1", "3V3_A")
net("R7", "2", "SDA_A")
place("R8", "Device", "R", BX - 5, 90, value="10k")
net("R8", "1", "3V3_A")
net("R8", "2", "SCL_A")
place("R9", "Device", "R", BX + 60, 90, value="10k")
net("R9", "1", "3V3_B")
net("R9", "2", "SDA_B")
place("R10", "Device", "R", BX + 85, 90, value="10k")
net("R10", "1", "3V3_B")
net("R10", "2", "SCL_B")

r = cmd("save_project", {})
print("save B:", r.get("success"))

print("=== PHASE C: Domain B fixed parts ===")
CX = 20
CY = 340
place("D4", "Device", "D_TVS", CX, CY, value="SMBJ26CA (interim, verify)")
net_long("D4", "1", "VEH_RAIL_B", stub=10.16)
net_long("D4", "2", "GND_B", stub=10.16)

# NOTE: no separate vehicle-feed connector -- design-inputs.md confirms A+ is
# carried by the 14-pos harness itself (J2 pin 2). A dedicated J4 would be
# redundant with that pin.

# PTC resettable fuse, harness A+ input -- same shared part as F1 (Littelfuse
# 2920L075/60MR, C207083). Sits between J2 pin 2 (raw harness feed) and the
# VEH_RAIL_B node that D4/Q2 hang off of.
place("F2", "Device", "Fuse", 50, 315, value="0.75A PTC (C207083)")
net("F2", "1", "VEH_RAIL_B_IN")
net("F2", "2", "VEH_RAIL_B")

place("Q2", "Transistor_FET", "AO3401A", CX + 60, CY, value="AO3401A (PLACEHOLDER, undersized)")
net("Q2", "2", "VEH_RAIL_B")
net("Q2", "3", "V_COIL_IN")
net("Q2", "1", "Q2_GATE")
place("R11", "Device", "R", CX + 60, CY + 40, value="10k")
net("R11", "1", "Q2_GATE")
net("R11", "2", "GND_B")

place("U4", "Regulator_Switching", "LM2596S-ADJ", CX + 130, CY, value="LM2596S-ADJ")
net_long("U4", "1", "V_COIL_IN", stub=10.16)
net_long("U4", "5", "V_COIL_IN", stub=10.16)
net_long("U4", "3", "GND_B", stub=10.16)
net_long("U4", "2", "SW_B", stub=10.16)
net_long("U4", "4", "FB_B", stub=10.16)

place("L2", "Device", "L", CX + 190, CY - 15, value="68uH")
net("L2", "1", "SW_B")
net("L2", "2", "COIL_9V")
place("D5", "Device", "D", CX + 190, CY + 15, value="SS56")
net("D5", "1", "GND_B")
net("D5", "2", "SW_B")

# CIN as 2x47uF in parallel (C11/C11B), same right-sizing logic as C1/C1B.
place("C11", "Device", "C_Polarized", CX + 105, CY + 40, value="47uF")
net("C11", "1", "V_COIL_IN")
net("C11", "2", "GND_B")
place("C11B", "Device", "C_Polarized", CX + 105, CY + 65, value="47uF")
net("C11B", "1", "V_COIL_IN")
net("C11B", "2", "GND_B")
place("C20", "Device", "C", CX + 105, CY + 90, value="1uF/100V")
net("C20", "1", "V_COIL_IN")
net("C20", "2", "GND_B")

place("C12", "Device", "C_Polarized", CX + 220, CY, value="220uF")
net("C12", "1", "COIL_9V")
net("C12", "2", "GND_B")
place("C13", "Device", "C", CX + 235, CY, value="1uF")
net("C13", "1", "COIL_9V")
net("C13", "2", "GND_B")
place("R12", "Device", "R", CX + 270, CY, value="7.5k")
net("R12", "1", "COIL_9V")
net_long("R12", "2", "FB_B", stub=10.16)
place("R13", "Device", "R", CX + 270, CY + 50, value="1.2k")
net_long("R13", "1", "FB_B", stub=10.16)
net("R13", "2", "GND_B")
# CFF -- feedforward cap, 9V instance only (circuit-draft.md: TI's Table 9-6
# lists a CFF value at the 9V row despite the >10V prose rule; skip on the
# 3.3V instance, well clear of both). In parallel with R12 (top FB resistor),
# Vout node to FB node.
place("C18", "Device", "C", CX + 270, CY - 25, value="1nF")
net("C18", "1", "COIL_9V")
net_long("C18", "2", "FB_B", stub=10.16)

place("U6", "Regulator_Linear", "AMS1117-3.3", CX + 130, CY + 80, value="AMS1117-3.3")
net_long("U6", "3", "COIL_9V", stub=10.16)
net_long("U6", "1", "GND_B", stub=10.16)
net_long("U6", "2", "3V3_B", stub=10.16)
place("C14", "Device", "C", CX + 105, CY + 100, value="10uF")
net("C14", "1", "COIL_9V")
net("C14", "2", "GND_B")
place("C15", "Device", "C", CX + 160, CY + 100, value="10uF")
net("C15", "1", "3V3_B")
net("C15", "2", "GND_B")

place("U5", "Interface_Expansion", "PCA9555PW", CX + 320, CY + 20, value="PCA9555PW")
net_long("U5", "24", "3V3_B", stub=15.24)
net_long("U5", "12", "GND_B", stub=15.24)
net_long("U5", "2", "GND_B", stub=15.24)
net_long("U5", "3", "GND_B", stub=15.24)
net_long("U5", "21", "GND_B", stub=15.24)
net_long("U5", "22", "SCL_B", stub=15.24)
net_long("U5", "23", "SDA_B", stub=15.24)
place("C16", "Device", "C", CX + 280, CY - 10, value="0.1uF")
net("C16", "1", "3V3_B")
net("C16", "2", "GND_B")

r = cmd("save_project", {})
print("save C:", r.get("success"))

print("=== PHASE D: 10 coil-driver channels + harness header ===")
PCA_PINS = {1: "4", 2: "5", 3: "6", 4: "7", 5: "8", 6: "9", 7: "10", 8: "11", 9: "13", 10: "14"}
DX0 = 20
DY0 = 520
CH_DX = 90
CH_DY = 100
for i in range(1, 11):
    col = (i - 1) % 5
    row = (i - 1) // 5
    x = DX0 + col * CH_DX
    y = DY0 + row * CH_DY

    net_long("U5", PCA_PINS[i], f"GPIO_CH{i}", stub=15.24)

    place(f"RB{i}", "Device", "R", x, y, value="10k")
    net(f"RB{i}", "1", f"GPIO_CH{i}")
    net(f"RB{i}", "2", f"BASE{i}")

    place(f"QN{i}", "Transistor_BJT", "MMBT2222A", x, y + 25, value="MMBT2222A")
    net(f"QN{i}", "1", f"BASE{i}")
    net(f"QN{i}", "2", "GND_B")
    net(f"QN{i}", "3", f"GATE{i}")

    place(f"RP{i}", "Device", "R", x + 40, y, value="10k")
    net(f"RP{i}", "1", "V_COIL_IN")
    net(f"RP{i}", "2", f"GATE{i}")

    place(f"QP{i}", "Transistor_FET", "AO3401A", x + 40, y + 25, value="AO3401A")
    net(f"QP{i}", "1", f"GATE{i}")
    net(f"QP{i}", "2", "V_COIL_IN")
    net_long(f"QP{i}", "3", f"COIL{i}", stub=10.16)

# Harness header -- RESOLVED per design-inputs.md: 0.1" (2.54mm) pitch,
# unshrouded, vertical THT male header, 14 positions. LCSC C2977586 (ZHOURI
# 2.54-1x40 breakable strip, snapped to 14 positions), Extended, 2.5A rated,
# -40..+105C. Pinout confirmed by count:
#   1        coil common return ("coil-"), shared across all 10 channels
#   2        A+ (harness carries the main feed -- no separate connector needed)
#   3-12     Coil 1+ .. Coil 10+ (one per channel, high-side switched)
#   13, 14   chassis (relay-board side)
# Pin1 and pins13/14 all tie to GND_B -- user call: common ground, no star
# topology needed (see design-inputs.md "Ground topology" note).
place("J2", "Connector_Generic", "Conn_01x14", DX0 + 5 * CH_DX + 30, DY0,
      value="Harness, 14-pos 0.1in THT (LCSC C2977586)")
net_long("J2", "1", "GND_B", stub=10.16)
net_long("J2", "2", "VEH_RAIL_B_IN", stub=10.16)
for i in range(1, 11):
    net_long("J2", str(i + 2), f"COIL{i}", stub=10.16)
net_long("J2", "13", "GND_B", stub=10.16)
net_long("J2", "14", "GND_B", stub=10.16)

r = cmd("save_project", {})
print("save D:", r.get("success"))

print("=== PHASE E: power flags ===")
for ref, x, y, net_name in [
    ("PF1", 200, 220, "GND_A"),
    ("PF8", 250, 20, "3V3_A"),
    ("PF6", 130, 20, "VIN_BUCK_A"),
    ("PF3", 20, 470, "GND_B"),
    ("PF5", 100, 470, "V_COIL_IN"),
    ("PF7", 280, 470, "COIL_9V"),
]:
    place(ref, "power", "PWR_FLAG", x, y, value="PWR_FLAG")
    net_long(ref, "1", net_name, stub=10.16)

r = cmd("save_project", {})
print("save E:", r.get("success"))
print(f"\nTOTAL FAILURES: {len(FAILS)}")
for f_ in FAILS:
    print(" ", f_[0], f_[1].get("reference") if isinstance(f_[1], dict) else f_[1])
