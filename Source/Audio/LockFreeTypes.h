#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// LockFreeTypes.h  –  Lock-free primitives for cross-thread communication.
//
//   AtomicSnapshotBuffer<T>  — double-buffer for scene snapshots
//   SPSCQueue<T, N>          — single-producer single-consumer ring buffer
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

// ── AtomicSnapshotBuffer ─────────────────────────────────────────────────────
// Writer (GL/message thread) writes to the back slot, then atomically swaps.
// Reader (audio thread) reads from the front slot.
// No lock, no spin-wait, no blocking.

template <typename T>
class AtomicSnapshotBuffer
{
public:
    void write(const T& value)
    {
        int back = 1 - readIndex.load(std::memory_order_acquire);
        slots[back] = value;
        readIndex.store(back, std::memory_order_release);
    }

    const T& read() const
    {
        int front = readIndex.load(std::memory_order_acquire);
        return slots[front];
    }

private:
    alignas(64) T slots[2] {};
    alignas(64) std::atomic<int> readIndex { 0 };
};

// ── SPSCQueue ────────────────────────────────────────────────────────────────
// Fixed-capacity single-producer single-consumer lock-free ring buffer.
// N must be a power of two.

template <typename T, std::size_t N>
class SPSCQueue
{
    static_assert((N & (N - 1)) == 0, "SPSCQueue capacity must be a power of two");

public:
    bool tryPush(const T& item)
    {
        const std::size_t h = head.load(std::memory_order_relaxed);
        const std::size_t nextH = (h + 1) & kMask;
        if (nextH == tail.load(std::memory_order_acquire))
            return false;
        buffer[h] = item;
        head.store(nextH, std::memory_order_release);
        return true;
    }

    bool tryPop(T& item)
    {
        const std::size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire))
            return false;
        item = buffer[t];
        tail.store((t + 1) & kMask, std::memory_order_release);
        return true;
    }

    bool isEmpty() const
    {
        return head.load(std::memory_order_acquire)
            == tail.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = N - 1;

    alignas(64) T buffer[N] {};
    alignas(64) std::atomic<std::size_t> head { 0 };
    alignas(64) std::atomic<std::size_t> tail { 0 };
};
