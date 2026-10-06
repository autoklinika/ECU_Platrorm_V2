#pragma once

#include "ecu/core/trace/replay.hpp"
#include "ecu/core/trace/trace.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace ecu::core::trace {

template <std::size_t RecordCapacity, std::size_t PayloadCapacity>
class MemoryTraceBuffer final
    : public ITraceSink,
      public IReplaySource {
  static_assert(RecordCapacity > 0U);
  static_assert(PayloadCapacity > 0U);

 public:
  void record(const TraceRecordView& record_view) noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};

    auto& target = records_[write_index_];

    ++sequence_;
    if (sequence_ == 0U) {
      ++sequence_;
    }

    target.header = record_view.header;
    target.header.sequence = sequence_;
    target.header.truncated =
        record_view.payload_size > PayloadCapacity;

    target.payload_size =
        std::min(record_view.payload_size, PayloadCapacity);

    if (target.payload_size != 0U &&
        record_view.payload != nullptr) {
      std::memcpy(
          target.payload.data(),
          record_view.payload,
          target.payload_size);
    }

    target.valid = true;

    write_index_ = (write_index_ + 1U) % RecordCapacity;
    if (count_ < RecordCapacity) {
      ++count_;
    }
  }

  ReplayStatus next(
      TraceRecordHeader& header,
      std::byte* payload,
      const std::size_t capacity,
      std::size_t& payload_size) noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};

    payload_size = 0U;

    if (!replay_active_ ||
        replay_next_sequence_ > replay_end_sequence_) {
      return ReplayStatus::end_of_stream;
    }

    const StoredRecord* source = nullptr;
    for (const auto& record : records_) {
      if (record.valid &&
          record.header.sequence == replay_next_sequence_) {
        source = &record;
        break;
      }
    }

    if (source == nullptr) {
      return ReplayStatus::io_error;
    }

    if (source->payload_size > capacity) {
      return ReplayStatus::buffer_too_small;
    }

    if (source->payload_size != 0U && payload == nullptr) {
      return ReplayStatus::invalid_argument;
    }

    header = source->header;
    payload_size = source->payload_size;

    if (payload_size != 0U) {
      std::memcpy(
          payload,
          source->payload.data(),
          payload_size);
    }

    ++replay_next_sequence_;
    return ReplayStatus::ok;
  }

  void reset() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};

    if (count_ == 0U) {
      replay_active_ = false;
      replay_next_sequence_ = 0U;
      replay_end_sequence_ = 0U;
      return;
    }

    replay_end_sequence_ = sequence_;
    replay_next_sequence_ =
        sequence_ - static_cast<TraceSequence>(count_) + 1U;
    replay_active_ = true;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return count_;
  }

  [[nodiscard]] TraceSequence last_sequence() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return sequence_;
  }

 private:
  struct StoredRecord {
    bool valid{false};
    TraceRecordHeader header{};
    std::array<std::byte, PayloadCapacity> payload{};
    std::size_t payload_size{0U};
  };

  mutable std::mutex mutex_{};
  std::array<StoredRecord, RecordCapacity> records_{};
  std::size_t write_index_{0U};
  std::size_t count_{0U};
  TraceSequence sequence_{0U};

  bool replay_active_{false};
  TraceSequence replay_next_sequence_{0U};
  TraceSequence replay_end_sequence_{0U};
};

}  // namespace ecu::core::trace
