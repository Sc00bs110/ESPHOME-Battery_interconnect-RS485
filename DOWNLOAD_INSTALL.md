# FINAL INSTALLATION - All Files Ready to Download

## ✅ Your Hardware Works!
RS-485 is receiving PACE BMS data perfectly (confirmed in logs: `7E` = frame start)

---

## 📥 Download Files

### Option 1: Download ZIP (Easiest)
[**pace_bms_component.zip**](computer:///mnt/user-data/outputs/pace_bms_component.zip) - Contains all 3 component files

### Option 2: Download Individual Files
1. [component_init.py](computer:///mnt/user-data/outputs/component_init.py) - Rename to `__init__.py`
2. [component_header.h](computer:///mnt/user-data/outputs/component_header.h) - Rename to `pace_bms_sniffer.h`
3. [component_code.cpp](computer:///mnt/user-data/outputs/component_code.cpp) - Rename to `pace_bms_sniffer.cpp`

### YAML Configuration
[pace-bms-sniffer-esp32.yaml](computer:///mnt/user-data/outputs/pace-bms-sniffer-esp32.yaml)

---

## 📁 Installation Steps

### Step 1: Extract/Rename Files

**If you downloaded the ZIP:**
```bash
cd /config/esphome
unzip pace_bms_component.zip
```

**If you downloaded individual files:**
- Rename `component_init.py` → `__init__.py`
- Rename `component_header.h` → `pace_bms_sniffer.h`
- Rename `component_code.cpp` → `pace_bms_sniffer.cpp`

### Step 2: Place Files

Your directory structure should be:
```
/config/esphome/
├── pace-bms-sniffer-esp32.yaml
├── secrets.yaml
└── components/
    └── pace_bms_sniffer/
        ├── __init__.py
        ├── pace_bms_sniffer.h
        └── pace_bms_sniffer.cpp
```

### Step 3: Compile

```bash
esphome clean pace-bms-sniffer-esp32.yaml
esphome run pace-bms-sniffer-esp32.yaml
```

---

## ✅ Expected Output

After flashing, check logs for:

```
[I][app:XXX] Running through setup()...
[C][pace_bms_sniffer:XXX] PACE BMS Sniffer:
[C][pace_bms_sniffer:XXX]   Protocol: Version 25
[I][pace_bms_sniffer:XXX] PACE BMS Sniffer initialized - Protocol v25
[I][pace_bms_sniffer:XXX] Listening for 8 battery packs (addresses 0x01-0x08)
[D][pace_bms_sniffer:XXX] Decoding Pack 2 data
[D][pace_bms_sniffer:XXX] Pack 2: V=52.45V SOC=85% Cells[0]=3.278V Temp[0]=25.3°C
```

---

## 🐛 Troubleshooting

### "does not name a type" error
- Make sure all 3 component files are in `/config/esphome/components/pace_bms_sniffer/`
- Run `esphome clean` before compiling

### "File not found" during compile
- Check the `external_components` path in YAML points to `components` (not `components/pace_bms_sniffer`)

### No BMS data in logs
- Your UART is working (confirmed)
- Component should now decode the frames automatically
- Check that BMS is using Protocol v25

---

## 📊 What You'll Get

Once working, the component will:
- ✅ Decode frames from up to 8 battery packs
- ✅ Log voltage, SOC, cell voltages, temperatures
- ✅ Calculate min/max/avg/delta cell voltages
- ⚠️ Some values (SOH, capacities, cycles) are unverified - check with PbmsTools

**Note**: This basic version logs data. To get Home Assistant sensors, you'd need to add sensor definitions (which we can do once this is working).

---

## 🎯 Summary

| Item | Status |
|------|--------|
| RS-485 Hardware | ✅ Working (confirmed in logs) |
| UART Receiving | ✅ Working (7E frames detected) |
| Component Files | ✅ Ready (3 files) |
| YAML Config | ✅ Ready |
| Next Step | Compile & flash |

Your hardware is perfect - just need to get the component compiled! 🚀
