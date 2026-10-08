#include "core/memory/alloc_gate.hpp"

#include <atomic>

namespace tulpar::engine {

namespace {
std::atomic<uint64_t> g_allocs{0};
std::atomic<uint64_t> g_frees{0};
std::atomic<uint64_t> g_bytes{0};
std::atomic<uint64_t> g_frame_start{0};
std::atomic<bool> g_in_frame{false};
thread_local uint64_t t_allocs = 0;
} // namespace

uint64_t AllocGate::total_allocations() { return g_allocs.load(std::memory_order_relaxed); }
uint64_t AllocGate::total_frees() { return g_frees.load(std::memory_order_relaxed); }
uint64_t AllocGate::total_bytes() { return g_bytes.load(std::memory_order_relaxed); }
uint64_t AllocGate::thread_allocations() { return t_allocs; }

void AllocGate::begin_frame() {
  g_frame_start.store(g_allocs.load(std::memory_order_relaxed), std::memory_order_relaxed);
  g_in_frame.store(true, std::memory_order_relaxed);
}

uint64_t AllocGate::end_frame() {
  g_in_frame.store(false, std::memory_order_relaxed);
  return g_allocs.load(std::memory_order_relaxed) - g_frame_start.load(std::memory_order_relaxed);
}

bool AllocGate::in_frame() { return g_in_frame.load(std::memory_order_relaxed); }

void AllocGate::on_alloc(uint64_t bytes) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  t_allocs++;
  g_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void AllocGate::on_free() { g_frees.fetch_add(1, std::memory_order_relaxed); }

} // namespace tulpar::engine
