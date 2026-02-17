#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

/*
    TODO:
    * Consider dropping perfect forwarding in push and add two overloads -
      one for lvalues and one for rvalues. It would simplify the code and make it more readable.
    * Consider memory access pattern to elements in the queue - false sharing, alignment
      Maybe it would be better in terms of performance to use a circular buffer instead of std::deque
      with atomic head and tail indices and no locks. But it would be more complex to implement and test.

*/
template <class T>
    requires std::default_initializable<T>
class spsc_queue
{
public:
    spsc_queue(std::size_t capacity) : capacity_(capacity), buffer_size_(capacity + 1)
    {
        if (capacity_ == 0)
        {
            throw std::invalid_argument("capacity must be > 0");
        }
        buffer_.reserve(buffer_size_);
    }

    // Non-blocking push. Returns false if the queue is full or closed_.
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

        // Publish element before publishing tail.
        tail_.store(next, std::memory_order_release);
        tail_.notify_one(); // wake consumer if waiting

        return true;
    }

    // Blocking push. Returns false if the queue gets closed.
    template <typename U>
        requires std::constructible_from<T, U &&>
    bool push(U &&item)
    {
        for (;;)
        {
            if (closed_.load(std::memory_order_acquire)) {
                return false;
            }

            const std::size_t t = tail_.load(std::memory_order_relaxed);
            const std::size_t next = (t + 1) % buffer_size_;

            if (next != head_.load(std::memory_order_acquire)) {
                buffer_[t] = T(std::forward<U>(item));
                tail_.store(next, std::memory_order_release);
                return true;
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

        // Publish head advance after consuming.
        head_.store(next, std::memory_order_release);
        head_.notify_one(); // wake producer if waiting

        return value;
    }

    // Blocking pop. Returns nullopt if the gets closed.
    std::optional<T> pop()
    {
        for (;;)
        {
            const std::size_t h = head_.load(std::memory_order_relaxed);
            const std::size_t t = tail_.load(std::memory_order_acquire);

            if (h != t) {
                T value = std::move(buffer_[h]);
                const std::size_t next = (h + 1) % buffer_size_;

                head_.store(next, std::memory_order_release);
                return value;
            }

            // empty
            if (closed_.load(std::memory_order_acquire)) {
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
        if (!closed_.exchange(true, std::memory_order_release)) {
            // Wake any producer/consumer blocked in atomic::wait loops.
            head_.notify_all();
            tail_.notify_all();
            closed_.notify_all();
        }
    }

    // NOTE: Destructor calling close() is only a best-effort wakeup.
    // The queue must outlive all threads that may access it.
    // Users must stop/join producer & consumer before destroying the queue.
    ~spsc_queue()
    {
        close();
    }

    // Let's not allow copying or moving the queue
    spsc_queue(const spsc_queue &) = delete;
    spsc_queue &operator=(const spsc_queue &) = delete;
    spsc_queue(spsc_queue &&) = delete;
    spsc_queue &operator=(spsc_queue &&) = delete;

private:
    const std::size_t capacity_;
    const std::size_t buffer_size_;
    alignas(64)
    std::atomic<size_t> head_ = 0;
    alignas(64)
    std::atomic<size_t> tail_ = 0;
    alignas(64)
    std::atomic<bool> closed_ = false;
    std::vector<T> buffer_;
};