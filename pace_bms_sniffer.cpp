#include "pace_bms_sniffer.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pace_bms_sniffer {

static const char *const TAG = "pace_bms_sniffer";

void PaceBMSSniffer::setup() {
  ESP_LOGI(TAG, "PACE BMS Sniffer initialized - Protocol v25");
  ESP_LOGI(TAG, "Listening for 8 battery packs (addresses 0x01-0x08)");
  ESP_LOGW(TAG, "UNVERIFIED SENSORS: SOH, Capacities, Cycles - Please verify with PbmsTools!");
}

void PaceBMSSniffer::loop() {
  // Read available data from UART
  while (available()) {
    uint8_t byte;
    read_byte(&byte);
    
    // Look for frame start
    if (byte == FRAME_START) {
      rx_buffer_.clear();
      rx_buffer_.push_back(byte);
    }
    else if (!rx_buffer_.empty()) {
      rx_buffer_.push_back(byte);
      
      // Check for frame end
      if (byte == FRAME_END) {
        if (rx_buffer_.size() > 10) {  // Minimum valid frame size
          process_frame(rx_buffer_);
        }
        rx_buffer_.clear();
      }
      
      // Prevent buffer overflow
      if (rx_buffer_.size() > MAX_FRAME_SIZE) {
        ESP_LOGW(TAG, "Frame too large, discarding");
        rx_buffer_.clear();
      }
    }
  }
}

void PaceBMSSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "PACE BMS Sniffer:");
  ESP_LOGCONFIG(TAG, "  Protocol: Version 25");
  ESP_LOGCONFIG(TAG, "  Batteries: 8 (0x01-0x08)");
  ESP_LOGCONFIG(TAG, "  Mode: Passive (RX only)");
}

uint8_t PaceBMSSniffer::hex_char_to_byte(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

uint16_t PaceBMSSniffer::parse_hex_word(const uint8_t *data) {
  // Parse 4 ASCII hex characters to uint16_t
  // Data is in LITTLE-ENDIAN format: "0CF0" means low byte first
  // Example: "0CF0" -> low=0xF0, high=0x0C -> 0x0CF0 = 3312
  uint8_t low_byte = (hex_char_to_byte(data[0]) << 4) | hex_char_to_byte(data[1]);
  uint8_t high_byte = (hex_char_to_byte(data[2]) << 4) | hex_char_to_byte(data[3]);
  return (high_byte << 8) | low_byte;
}

uint32_t PaceBMSSniffer::parse_hex_dword(const uint8_t *data) {
  // Parse 8 ASCII hex characters to uint32_t
  return (static_cast<uint32_t>(parse_hex_word(data)) << 16) |
         parse_hex_word(data + 4);
}

void PaceBMSSniffer::process_frame(const std::vector<uint8_t> &frame) {
  // Frame format: ~25[ADDR][CMD][DATA][CHECKSUM]\r
  
  if (frame.size() < 15) {
    return;  // Too short
  }
  
  // Check protocol version
  if (frame[1] != '2' || frame[2] != '5') {
    return;  // Not protocol v25
  }
  
  // Extract address (2 ASCII chars)
  uint8_t addr = (hex_char_to_byte(frame[3]) << 4) | hex_char_to_byte(frame[4]);
  
  if (addr < 1 || addr > 8) {
    return;  // Invalid address
  }
  
  // Extract command (2 ASCII chars)
  uint8_t cmd = (hex_char_to_byte(frame[5]) << 4) | hex_char_to_byte(frame[6]);
  
  // Command 0x46 = Read data
  if (cmd == 0x46) {
    // Check if this is a response (contains "00" after command)
    if (frame.size() > 10 && frame[7] == '0' && frame[8] == '0') {
      // This is a response with data
      decode_analog_data(frame, addr);
    }
  }
}

bool PaceBMSSniffer::decode_analog_data(const std::vector<uint8_t> &frame, uint8_t pack_addr) {
  // Frame: ~25[ADDR]4600[TYPE][DATA][CHECKSUM]\r
  // Response types:
  // - 1096 = Full analog data (cell voltages, temps, etc.)
  // - 4E00 = Status data (minimal)
  
  if (frame.size() < 50) {
    return false;  // Too short for analog data
  }
  
  uint8_t pack_idx = pack_addr - 1;
  if (pack_idx >= NUM_PACKS) {
    return false;
  }
  
  // Check response type (position 9-12: "1096" for full data)
  if (frame[9] != '1' || frame[10] != '0' || frame[11] != '9' || frame[12] != '6') {
    // Not full analog data, might be status
    return false;
  }
  
  // Data starts at position 19 (after ~25[AA]4600109600[AA]1)
  // Position 19: Pack status/ID
  size_t pos = 21;  // Start of actual data
  
  if (frame.size() < pos + (NUM_CELLS * 4) + (NUM_TEMPS * 5) + 20) {
    ESP_LOGW(TAG, "Frame too short for full data decode");
    return false;
  }
  
  ESP_LOGD(TAG, "Decoding Pack %d data", pack_addr);
  
  // Debug: Show first few bytes of frame
  ESP_LOGD(TAG, "Frame preview: %c%c%c%c%c%c%c%c%c%c pos=%d size=%d", 
           frame[0], frame[1], frame[2], frame[3], frame[4],
           frame[5], frame[6], frame[7], frame[8], frame[9], pos, frame.size());
  
  // Parse 16 cell voltages (4 ASCII hex chars each = 2 bytes)
  for (size_t i = 0; i < NUM_CELLS; i++) {
    uint16_t raw_mv = parse_hex_word(&frame[pos + (i * 4)]);
    if (i == 0) {
      // Debug first cell
      ESP_LOGD(TAG, "Cell 0: chars='%c%c%c%c' raw=0x%04X=%d mV=%.3fV", 
               frame[pos], frame[pos+1], frame[pos+2], frame[pos+3],
               raw_mv, raw_mv, raw_mv / 1000.0f);
    }
    pack_data_[pack_idx].cell_voltages[i] = raw_mv / 1000.0f;  // Convert mV to V
  }
  pos += NUM_CELLS * 4;  // Skip cell voltages
  
  // Parse 6 temperatures (5 ASCII chars each, format: "60B88")
  // First char seems to be a type/status, then 4 hex chars for temp
  for (size_t i = 0; i < NUM_TEMPS; i++) {
    pos++;  // Skip type byte
    uint16_t raw_temp = parse_hex_word(&frame[pos]);
    pack_data_[pack_idx].temperatures[i] = raw_temp / 100.0f;  // Divide by 100 for °C
    pos += 4;
  }
  
  // After temperatures comes the critical section with current, voltage, SOC, etc.
  // Based on analysis:
  // Position varies, but pattern observed:
  // - 4 bytes: unknown
  // - 4 bytes: Pack voltage (hex word)
  // - 4 bytes: Current or capacity data
  // - More data...
  
  if (frame.size() < pos + 50) {
    ESP_LOGW(TAG, "Not enough data for extended fields");
    pack_data_[pack_idx].valid = true;
    calculate_pack_stats(pack_idx);
    publish_pack_data(pack_idx);
    return true;  // Still valid, just missing extended data
  }
  
  // Skip 4 bytes (0000 padding observed)
  pos += 4;
  
  // Parse pack voltage (4 hex chars)
  // Example: CE83 = 52867 / 100 = 528.67V... that's too high
  // Try: 52867 / 1000 = 52.867V ✓
  uint16_t raw_voltage = parse_hex_word(&frame[pos]);
  pack_data_[pack_idx].voltage = raw_voltage / 100.0f;  // Divide by 100 for volts
  pos += 4;
  
  // UNVERIFIED SECTION - These are best guesses!
  // Parse remaining capacity (4 hex chars)
  // Example Pack 2: 132C = 4908 -> 49.08 Ah (close to 47.93 Ah)
  uint16_t raw_remaining = parse_hex_word(&frame[pos]);
  pack_data_[pack_idx].remaining_cap = raw_remaining / 100.0f;  // ⚠️ UNVERIFIED!
  pos += 4;
  
  // Parse SOC (4 hex chars)
  // Example: 032C = 812 -> Maybe 81.2%? Or encoded differently?
  uint16_t raw_soc_field = parse_hex_word(&frame[pos]);
  // Try: First 2 chars = SOC
  pack_data_[pack_idx].soc = (raw_soc_field >> 8) & 0xFF;  // ⚠️ UNVERIFIED!
  pos += 4;
  
  // Parse full capacity or design capacity
  // Example: 3300 = 13056 -> 130.56 Ah? (Pack 2 full cap = 113.15)
  // Maybe it's 1130.56 / 10 = 113.056? Let's try...
  uint16_t raw_cap_field = parse_hex_word(&frame[pos]);
  pack_data_[pack_idx].full_cap = raw_cap_field / 100.0f;  // ⚠️ UNVERIFIED!
  pos += 4;
  
  // Next section appears to be ASCII characters and metadata
  // Skipping complex parsing for now, need more analysis
  
  // Set defaults for fields we couldn't decode yet
  pack_data_[pack_idx].soh = 100;  // ⚠️ HARDCODED - UNVERIFIED!
  pack_data_[pack_idx].design_cap = 100.0f;  // ⚠️ HARDCODED - UNVERIFIED!
  pack_data_[pack_idx].cycles = 0;  // ⚠️ UNVERIFIED!
  pack_data_[pack_idx].current = 0;  // ⚠️ UNVERIFIED!
  
  pack_data_[pack_idx].valid = true;
  pack_data_[pack_idx].last_update = millis();
  
  calculate_pack_stats(pack_idx);
  publish_pack_data(pack_idx);
  
  return true;
}

void PaceBMSSniffer::calculate_pack_stats(uint8_t pack_idx) {
  if (pack_idx >= NUM_PACKS) return;
  
  auto &pack = pack_data_[pack_idx];
  
  // Calculate min/max/avg cell voltages
  pack.min_cell_v = pack.cell_voltages[0];
  pack.max_cell_v = pack.cell_voltages[0];
  float sum = 0;
  
  for (size_t i = 0; i < NUM_CELLS; i++) {
    if (pack.cell_voltages[i] < pack.min_cell_v) {
      pack.min_cell_v = pack.cell_voltages[i];
    }
    if (pack.cell_voltages[i] > pack.max_cell_v) {
      pack.max_cell_v = pack.cell_voltages[i];
    }
    sum += pack.cell_voltages[i];
  }
  
  pack.avg_cell_v = sum / NUM_CELLS;
  pack.delta_cell_v = pack.max_cell_v - pack.min_cell_v;
}

void PaceBMSSniffer::publish_pack_data(uint8_t pack_idx) {
  if (pack_idx >= NUM_PACKS) return;
  
  auto &pack = pack_data_[pack_idx];
  auto &sensors = pack_sensors_[pack_idx];
  
  if (!pack.valid) return;
  
  // Publish confirmed sensors
  if (sensors.current) sensors.current->publish_state(pack.current);
  if (sensors.voltage) sensors.voltage->publish_state(pack.voltage);
  if (sensors.soc) sensors.soc->publish_state(pack.soc);
  
  // Publish temperatures
  for (size_t i = 0; i < NUM_TEMPS && i < 6; i++) {
    if (sensors.temps[i]) {
      sensors.temps[i]->publish_state(pack.temperatures[i]);
    }
  }
  
  // Publish cell voltages
  for (size_t i = 0; i < NUM_CELLS; i++) {
    if (sensors.cell_voltages[i]) {
      sensors.cell_voltages[i]->publish_state(pack.cell_voltages[i]);
    }
  }
  
  // Publish UNVERIFIED sensors (marked for checking)
  if (sensors.soh) sensors.soh->publish_state(pack.soh);
  if (sensors.remaining_cap) sensors.remaining_cap->publish_state(pack.remaining_cap);
  if (sensors.full_cap) sensors.full_cap->publish_state(pack.full_cap);
  if (sensors.design_cap) sensors.design_cap->publish_state(pack.design_cap);
  if (sensors.cycles) sensors.cycles->publish_state(pack.cycles);
  
  // Publish calculated values
  if (sensors.min_cell) sensors.min_cell->publish_state(pack.min_cell_v);
  if (sensors.max_cell) sensors.max_cell->publish_state(pack.max_cell_v);
  if (sensors.avg_cell) sensors.avg_cell->publish_state(pack.avg_cell_v);
  if (sensors.delta_cell) sensors.delta_cell->publish_state(pack.delta_cell_v);
  
  ESP_LOGD(TAG, "Pack %d: V=%.2fV SOC=%.0f%% Cells[0]=%.3fV Temp[0]=%.1f°C",
           pack_idx + 1, pack.voltage, pack.soc, pack.cell_voltages[0], pack.temperatures[0]);
}

}  // namespace pace_bms_sniffer
}  // namespace esphome