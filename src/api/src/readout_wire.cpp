#include "ecu/api/readout_wire.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace ecu::api::v1 {
namespace {
constexpr std::string_view kMagic{"ECU_COMPLETED_DTC_V1"};

bool pop_line(std::string_view& input, std::string_view& output) noexcept {
  const auto end = input.find('\n');
  if (end == std::string_view::npos) return false;
  output = input.substr(0U, end);
  input.remove_prefix(end + 1U);
  if (output.find_first_of("\r\0", 0U, 2U) != std::string_view::npos)
    return false;
  return true;
}

bool pop_field(std::string_view& input, std::string_view key,
               std::string_view& output) noexcept {
  std::string_view line;
  if (!pop_line(input, line) || line.substr(0U, key.size()) != key)
    return false;
  output = line.substr(key.size());
  return !output.empty();
}

bool decimal(std::string_view input, std::uint64_t& result) noexcept {
  if (input.empty() || input.size() > 20U ||
      (input.size() > 1U && input.front() == '0'))
    return false;
  for (const char c : input)
    if (c < '0' || c > '9') return false;
  const auto parsed = std::from_chars(input.data(), input.data() + input.size(),
                                     result);
  return parsed.ec == std::errc{} &&
         parsed.ptr == input.data() + input.size();
}

constexpr char kHex[] = "0123456789ABCDEF";

int nibble(char character) noexcept {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'A' && character <= 'F')
    return character - 'A' + 10;
  return -1;
}

bool hex_byte(std::string_view source, std::uint8_t& value) noexcept {
  if (source.size() != 2U) return false;
  const auto high = nibble(source[0]);
  const auto low = nibble(source[1]);
  if (high < 0 || low < 0) return false;
  value = static_cast<std::uint8_t>((high << 4) | low);
  return true;
}

bool valid_code(std::string_view value) noexcept {
  if (value.size() != 6U) return false;
  for (const char c : value)
    if (nibble(c) < 0) return false;
  return true;
}

bool valid_record(const CompletedDtcReadout& value) noexcept {
  if (value.captured_at_unix_ms == 0U || value.profile_id == 0U ||
      value.completed_generation == 0U || value.dtcs.protocol != "uds" ||
      value.dtcs.requested_status_mask == 0U ||
      value.dtcs.entries.size() > kMaxReadoutDtcs)
    return false;
  for (std::size_t i = 0U; i < value.dtcs.entries.size(); ++i) {
    const auto& entry = value.dtcs.entries[i];
    if (!valid_code(entry.code) ||
        (entry.status_mask &
         static_cast<std::uint8_t>(~value.dtcs.status_availability_mask)) != 0U)
      return false;
    for (std::size_t j = 0U; j < i; ++j)
      if (entry.code == value.dtcs.entries[j].code) return false;
  }
  return true;
}

void append_byte(std::uint8_t value, std::string& output) {
  output += kHex[value >> 4U];
  output += kHex[value & 0x0fU];
}

}  // namespace

bool encode_completed_readout(const CompletedDtcReadout& value,
                              std::string& destination) {
  destination.clear();
  if (!valid_record(value)) return false;
  std::string output{kMagic};
  output += "\n";
  output += "captured_at_unix_ms=" +
            std::to_string(value.captured_at_unix_ms) + "\n";
  output += "profile_id=" + std::to_string(value.profile_id) + "\n";
  output += "completed_generation=" +
            std::to_string(value.completed_generation) + "\n";
  output += "status_availability=";
  append_byte(value.dtcs.status_availability_mask, output);
  output += "\nrequested_mask=";
  append_byte(value.dtcs.requested_status_mask, output);
  output += "\nentry_count=" +
            std::to_string(value.dtcs.entries.size()) + "\n";
  for (const auto& entry : value.dtcs.entries) {
    output += entry.code + ",";
    append_byte(entry.status_mask, output);
    output += '\n';
  }
  output += "END\n";
  if (output.size() > kMaxReadoutBytes) return false;
  destination = std::move(output);
  return true;
}

ReadResult<CompletedDtcReadout> decode_completed_readout(
    std::string_view source) {
  if (source.empty() || source.size() > kMaxReadoutBytes)
    return {ReadStatus::invalid_snapshot, {}};

  std::string_view line;
  if (!pop_line(source, line) || line != kMagic)
    return {ReadStatus::invalid_snapshot, {}};

  CompletedDtcReadout value{};
  std::uint64_t number = 0U;
  if (!pop_field(source, "captured_at_unix_ms=", line) ||
      !decimal(line, number) || number == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  value.captured_at_unix_ms = number;

  if (!pop_field(source, "profile_id=", line) || !decimal(line, number) ||
      number == 0U || number > (std::numeric_limits<std::uint32_t>::max)())
    return {ReadStatus::invalid_snapshot, {}};
  value.profile_id = static_cast<std::uint32_t>(number);

  if (!pop_field(source, "completed_generation=", line) ||
      !decimal(line, number) || number == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  value.completed_generation = number;

  if (!pop_field(source, "status_availability=", line) ||
      !hex_byte(line, value.dtcs.status_availability_mask) ||
      !pop_field(source, "requested_mask=", line) ||
      !hex_byte(line, value.dtcs.requested_status_mask) ||
      value.dtcs.requested_status_mask == 0U ||
      !pop_field(source, "entry_count=", line) ||
      !decimal(line, number) || number > kMaxReadoutDtcs)
    return {ReadStatus::invalid_snapshot, {}};

  const auto expected = static_cast<std::size_t>(number);
  value.dtcs.protocol = "uds";
  value.dtcs.entries.reserve(expected);
  for (std::size_t i = 0U; i < expected; ++i) {
    if (!pop_line(source, line) || line.size() != 9U ||
        line[6] != ',' || !valid_code(line.substr(0U, 6U)))
      return {ReadStatus::invalid_snapshot, {}};
    std::uint8_t status = 0U;
    if (!hex_byte(line.substr(7U), status))
      return {ReadStatus::invalid_snapshot, {}};
    value.dtcs.entries.push_back({std::string{line.substr(0U, 6U)}, status});
  }
  if (!pop_line(source, line) || line != "END" || !source.empty() ||
      !valid_record(value))
    return {ReadStatus::invalid_snapshot, {}};
  return {ReadStatus::ok, std::move(value)};
}

namespace {
constexpr std::string_view kSacParameterMagic{
    "ECU_COMPLETED_SAC_PARAMETERS_V1"};

bool valid_sac_parameters(const CompletedSacParameters& value) noexcept {
  return value.captured_at_unix_ms != 0U &&
         (value.profile_id == 0xDAF00025U ||
          value.profile_id == 0xDAF00050U) &&
         value.completed_generation != 0U &&
         value.permanent_decivolt <= 600U &&
         value.ignition_decivolt <= 600U &&
         (!value.pressure1_valid ||
          (value.pgn_feae_observed && value.pressure1_centibar <= 2024U)) &&
         (!value.pressure2_valid ||
          (value.pgn_feae_observed && value.pressure2_centibar <= 2024U)) &&
         (value.pressure1_valid || value.pressure1_centibar == 0U) &&
         (value.pressure2_valid || value.pressure2_centibar == 0U);
}

bool read_numeric_field(std::string_view& src, const std::string_view key,
                        std::uint64_t& value) noexcept {
  std::string_view line;
  return pop_field(src, key, line) && decimal(line, value);
}

bool read_optional_pressure(std::string_view& src,
                            const std::string_view key,
                            bool& available, std::uint16_t& amount) noexcept {
  std::string_view value;
  if (!pop_field(src, key, value)) return false;
  if (value == "NA") {
    available = false;
    amount = 0U;
    return true;
  }
  std::uint64_t parsed = 0U;
  if (!decimal(value, parsed) || parsed > 2024U) return false;
  amount = static_cast<std::uint16_t>(parsed);
  available = true;
  return true;
}
}  // namespace

bool encode_completed_sac_parameters(
    const CompletedSacParameters& value, std::string& destination) {
  destination.clear();
  if (!valid_sac_parameters(value)) return false;
  std::string payload{kSacParameterMagic};
  payload += "\n";
  payload += "captured_at_unix_ms=" +
             std::to_string(value.captured_at_unix_ms) + "\n";
  payload += "profile_id=" + std::to_string(value.profile_id) + "\n";
  payload += "completed_generation=" +
             std::to_string(value.completed_generation) + "\n";
  payload += "permanent_decivolt=" +
             std::to_string(value.permanent_decivolt) + "\n";
  payload += "ignition_decivolt=" +
             std::to_string(value.ignition_decivolt) + "\n";
  payload += "pgn_feae_observed=";
  payload += value.pgn_feae_observed ? "1\n" : "0\n";
  payload += "pressure1_centibar=";
  payload += value.pressure1_valid ?
      std::to_string(value.pressure1_centibar) : "NA";
  payload += "\npressure2_centibar=";
  payload += value.pressure2_valid ?
      std::to_string(value.pressure2_centibar) : "NA";
  payload += "\nEND\n";
  if (payload.size() > kMaxReadoutBytes) return false;
  destination = std::move(payload);
  return true;
}

ReadResult<CompletedSacParameters> decode_completed_sac_parameters(
    std::string_view source) {
  if (source.empty() || source.size() > kMaxReadoutBytes)
    return {ReadStatus::invalid_snapshot, {}};
  std::string_view line;
  if (!pop_line(source, line) || line != kSacParameterMagic)
    return {ReadStatus::invalid_snapshot, {}};
  CompletedSacParameters result{};
  std::uint64_t number = 0U;
  if (!read_numeric_field(source, "captured_at_unix_ms=", number) ||
      number == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  result.captured_at_unix_ms = number;
  if (!read_numeric_field(source, "profile_id=", number) ||
      number > (std::numeric_limits<std::uint32_t>::max)())
    return {ReadStatus::invalid_snapshot, {}};
  result.profile_id = static_cast<std::uint32_t>(number);
  if (!read_numeric_field(source, "completed_generation=", number) ||
      number == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  result.completed_generation = number;
  if (!read_numeric_field(source, "permanent_decivolt=", number) ||
      number > 600U)
    return {ReadStatus::invalid_snapshot, {}};
  result.permanent_decivolt = static_cast<std::uint16_t>(number);
  if (!read_numeric_field(source, "ignition_decivolt=", number) ||
      number > 600U)
    return {ReadStatus::invalid_snapshot, {}};
  result.ignition_decivolt = static_cast<std::uint16_t>(number);
  if (!pop_field(source, "pgn_feae_observed=", line) ||
      (line != "0" && line != "1"))
    return {ReadStatus::invalid_snapshot, {}};
  result.pgn_feae_observed = line == "1";
  if (!read_optional_pressure(source, "pressure1_centibar=",
                              result.pressure1_valid,
                              result.pressure1_centibar) ||
      !read_optional_pressure(source, "pressure2_centibar=",
                              result.pressure2_valid,
                              result.pressure2_centibar) ||
      !pop_line(source, line) || line != "END" ||
      !source.empty() || !valid_sac_parameters(result))
    return {ReadStatus::invalid_snapshot, {}};
  return {ReadStatus::ok, result};
}

}  // namespace ecu::api::v1
