#include <concepts>
#include <thread>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <new>
#include <atomic>

/// @class atomic_spin_spsc_queue
/// @brief A single-producer, single-consumer (SPSC) bounded queue.
///
/// @tparam T The type of elements stored in the queue. Must be default-initializable and movable.
///
/// @details
/// Ring-buffer based SPSC queue using atomic head_ and tail_ indices.
///
/// - Exactly one producer modifies tail_.
/// - Exactly one consumer modifies head_.
/// - No locks; synchronization via atomics only.
/// - Blocking push()/pop() use bounded spinning with periodic yield()
///
/// The internal buffer size is (capacity + 1). One slot is intentionally unused so that:
///   * empty : head_ == tail_
///   * full  : (tail_ + 1) % buffer_size_ == head_
/// This avoids a shared atomic size counter and any shared RMW operations in the hot path.
///
/// - Memory ordering:
///   * Relaxed for loading indices in the thread that modifies them.
///   * Release-acquire for all other cases:
///     - closed_ is released by close() and acquired in push()/pop().
///     - tail_ is released by push() and acquired in pop().
///     - head_ is released by pop() and acquired in push().
///
/// @note The queue is non-copyable and non-movable. The queue must outlive
/// all threads accessing it. Users are responsible for stopping and joining
/// producer and consumer threads before destroying the queue.
///
/// @warning This queue is NOT thread-safe for multiple producers or consumers.

template <class T>
    requires std::default_initializable<T> && std::movable<T>
class atomic_spin_spsc_queue
{
public:
    atomic_spin_spsc_queue(std::size_t capacity) : capacity_(capacity), buffer_size_(capacity + 1), buffer_(buffer_size_)
    {
        if (capacity_ == 0)
        {
            throw std::invalid_argument("capacity must be > 0");
        }
    }

    // Non-blocking push. Returns false if the queue is full or closed.
    template <typename U>
        requires std::constructible_from<T, U &&>
    bool try_push(U &&item)
    {
        if (closed_.load(std::memory_order_acquire))
        {
            return false;
        }
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (t + 1) % buffer_size_;

        // Full if advancing tail would collide with head.
        if (next == head_.load(std::memory_order_acquire))
        {
            return false;
        }

        buffer_[t] = T(std::forward<U>(item));

        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Blocking push. Returns false if the queue gets closed.
    template <typename U>
        requires std::constructible_from<T, U &&>
    bool push(U &&item)
    {
        for (std::size_t spin = 0;;)
        {
            if (closed_.load(std::memory_order_acquire))
            {
                return false;
            }

            const std::size_t t = tail_.load(std::memory_order_relaxed);
            const std::size_t next = (t + 1) % buffer_size_;

            // Full if advancing tail would collide with head.
            if (next != head_.load(std::memory_order_acquire))
            {
                buffer_[t] = T(std::forward<U>(item));
                tail_.store(next, std::memory_order_release);
                return true;
            }

            if (++spin >= yield_after_)
            {
                std::this_thread::yield();
                spin = 0;
            }
        }
    }

    // Non-blocking pop. Returns nullopt if the queue is empty.
    std::optional<T> try_pop()
    {
        const std::size_t h = head_.load(std::memory_order_relaxed);

        // Empty if head catches tail.
        if (h == tail_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }

        T value = std::move(buffer_[h]);
        const std::size_t next = (h + 1) % buffer_size_;

        head_.store(next, std::memory_order_release);
        return value;
    }

    // Blocking pop. Returns nullopt if the queue is empty and gets closed.
    std::optional<T> pop()
    {
        for (std::size_t spin = 0;;)
        {
            const std::size_t h = head_.load(std::memory_order_relaxed);

            // Empty if head catches tail.
            if (h != tail_.load(std::memory_order_acquire))
            {
                T value = std::move(buffer_[h]);
                const std::size_t next = (h + 1) % buffer_size_;

                head_.store(next, std::memory_order_release);
                return value;
            }

            if (++spin >= yield_after_)
            {
                std::this_thread::yield();
                spin = 0;
            }

            if (closed_.load(std::memory_order_acquire))
            {
                return std::nullopt;
            }
        }
    }
    std::size_t capacity() const
    {
        return capacity_;
    }

    void close()
    {
        closed_.store(true, std::memory_order_release);
    }

    // Destructor calling close() is only a best-effort wakeup.
    // The queue must outlive all threads that may access it.
    // Users must stop/join producer & consumer before destroying the queue.
    ~atomic_spin_spsc_queue()
    {
        close();
    }

    // Let's not allow copying or moving the queue
    atomic_spin_spsc_queue(const atomic_spin_spsc_queue &) = delete;
    atomic_spin_spsc_queue &operator=(const atomic_spin_spsc_queue &) = delete;
    atomic_spin_spsc_queue(atomic_spin_spsc_queue &&) = delete;
    atomic_spin_spsc_queue &operator=(atomic_spin_spsc_queue &&) = delete;

private:
    const std::size_t capacity_;
    const std::size_t buffer_size_;
    std::vector<T> buffer_;
    static constexpr std::size_t yield_after_ = 1024;
    static constexpr std::size_t cacheline_size = 64;
    alignas(cacheline_size)
        std::atomic<std::size_t> head_ = 0;
    alignas(cacheline_size)
        std::atomic<std::size_t> tail_ = 0;
    alignas(cacheline_size)
        std::atomic<bool> closed_ = false;
};