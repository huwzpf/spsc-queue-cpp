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
    spsc_queue(std::size_t capacity) : capacity_(capacity)
    {
        if (capacity_ == 0)
        {
            throw std::invalid_argument("capacity must be > 0");
        }
        buffer_.reserve(capacity_);
    }

    // Non-blocking push. Returns false if the queue is full or closed_.
    template <typename U>
        requires std::constructible_from<T, U &&>
    bool try_push(U &&item)
    {
        // It is safe to read size here and increment later since only this thread increments it
        // Alternatively, could do fetch_add and fetch_sub if it exceeds
        if (size() >= capacity_ || closed_.load())
        {
            return false;
        }

        buffer_[tail_] = T(std::forward<U>(item));
        tail_ = (tail_ + 1) % capacity_;

        size_.fetch_add(1);
        size_.notify_one();
        return true;
    }

    // Blocking push. Returns false if the queue gets closed.
    template <typename U>
        requires std::constructible_from<T, U &&>
    bool push(U &&item)
    {
        if (closed_.load())
        {
            return false;
        }

        // It is safe to read size here and increment later since only this thread increments it
        // Alternatively, could do fetch_add and fetch_sub if it exceeds
        if (size() >= capacity_)
        {
            // Read wait until size changes, which means either an item was popped or the queue got closed
            // Do it in while loop to handle spurious wakeups
            auto s = size();
            while (s >= capacity_)
            {
                if (closed_.load())
                {
                    return false;
                }
                // Wait until size changes, which means either an item was popped or the queue got closed
                size_.wait(s);

                s = size();
            }
        }

        buffer_[tail_] = T(std::forward<U>(item));
        tail_ = (tail_ + 1) % capacity_;

        size_.fetch_add(1);
        size_.notify_one();
        return true;
    }

    // Non-blocking pop. Returns nullopt if the queue is empty.
    std::optional<T> try_pop()
    {
        // It is safe to read size here and decrement later since only this thread decrements it
        // Alternatively, could do fetch_sub and fetch_add if zero
        if (size() == 0 || closed_.load())
        {
            return std::nullopt;
        }

        T item = buffer_[head_];
        head_ = (head_ + 1) % capacity_;

        size_.fetch_sub(1);
        size_.notify_one();
        return item;
    }

    // Blocking pop. Returns nullopt if the gets closed.
    std::optional<T> pop()
    {
        if (closed_.load())
        {
            return std::nullopt;
        }

        // It is safe to read size here and decrement later since only this thread decrements it
        // Alternatively, could do fetch_sub and fetch_add if zero
        if (size_ == 0)
        {
            // Read wait until size changes, which means either an item was pushed or the queue got closed
            // Do it in while loop to handle spurious wakeups
            auto s = size();
            while (s == 0)
            {
                if (closed_.load())
                {
                    return std::nullopt;
                }
                // Wait until size changes, which means either an item was pushed or the queue got closed
                size_.wait(s);

                s = size();
            }
        }

        T item = buffer_[head_];
        head_ = (head_ + 1) % capacity_;

        size_.fetch_sub(1);
        size_.notify_one();
        return item;
    }

    std::size_t size() const
    {
        return size_.load();
    }

    std::size_t capacity() const
    {
        return capacity_;
    }

    void close()
    {
        closed_.store(true);
        size_.store(0);
        size_.notify_all();
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
    std::atomic<size_t> size_ = 0;
    // Written only by consumer thread, read by producer thread. No need for atomicity as long as there's only one producer and one consumer.
    size_t head_ = 0;
    // Written only by producer thread, read by consumer thread. No need for atomicity as long as there's only one producer and one consumer.
    size_t tail_ = 0;
    std::atomic<bool> closed_ = false;
    std::vector<T> buffer_;
};