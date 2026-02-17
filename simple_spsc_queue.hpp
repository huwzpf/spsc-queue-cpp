#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

/// @class spsc_queue
/// @brief A single-producer, single-consumer (SPSC) bounded queue.
///
/// @tparam T The type of elements stored in the queue.
///
/// @details
/// Mutex and condition variable based SPSC queue implementation.
/// 
/// - Uses a deque for storage with mutex-protected access.
/// - Blocking push()/pop() use condition variables for efficient waiting.
/// - The queue is non-copyable and non-movable.
/// - NOT thread-safe for multiple producers or consumers.
///
/// @warning The queue must outlive all threads accessing it.
/// Users are responsible for stopping and joining producer and consumer
/// threads before destroying the queue.

template <class T>
class spsc_queue {
public:
    spsc_queue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("capacity must be > 0");
        }
    }

     // Non-blocking push. Returns false if the queue is full or closed_.
    template <typename U>
    requires std::constructible_from<T, U&&>
    bool try_push(U&& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        return locked_push(lock, std::forward<U>(item));
    }

     // Blocking push. Returns false if the queue gets closed.
    template<typename U>
    requires std::constructible_from<T, U&&>
    bool push (U&& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        // continue if queue is closed_ or it is smaller than capacity
        producer_cv_.wait(lock, [this]{ return closed_ || q_.size() < capacity_;});

        return locked_push(lock, std::forward<U>(item));
    }

    // Non-blocking pop. Returns nullopt if the queue is empty.
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        return locked_pop(lock);
    }

    // Blocking pop. Returns nullopt if the queue is empty and gets closed.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        // continue if queue is closed_ or has at least one element
        consumer_cv_.wait(lock, [this]{ return closed_ || !q_.empty();});
        
        return locked_pop(lock);
    }


    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return q_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    
    void close(){ 
        {
            std::lock_guard<std::mutex> lock(mtx_);
            closed_ = true;
        }

        consumer_cv_.notify_all();
        producer_cv_.notify_all();
    }

    // NOTE: Destructor calling close() is only a best-effort wakeup. 
    // The queue must outlive all threads that may access it.
    // Users must stop/join producer & consumer before destroying the queue.
    ~spsc_queue() {
        close();
    }

    // Let's not allow copying or moving the queue 
    spsc_queue(const spsc_queue&) = delete;
    spsc_queue& operator=(const spsc_queue&) = delete;
    spsc_queue(spsc_queue&&) = delete;
    spsc_queue& operator=(spsc_queue&&) = delete;

private:
    template<typename U>
    requires std::constructible_from<T, U&&>
    bool locked_push(std::unique_lock<std::mutex>& lock, U&& item) {
        if (q_.size() >= capacity_ || closed_) {
            return false;
        }

        q_.push_back(std::forward<U>(item));

        lock.unlock();
        consumer_cv_.notify_one();
    
        return true;
    }

    std::optional<T> locked_pop(std::unique_lock<std::mutex>& lock) {
        if (q_.empty()) {
            return std::nullopt;
        }

        T item = std::move(q_.front());
        q_.pop_front();

        lock.unlock();
        producer_cv_.notify_one();
    
        return item;
    }

    const std::size_t capacity_;
    std::condition_variable producer_cv_;
    std::condition_variable consumer_cv_;
    std::mutex mtx_;
    bool closed_ = false;
    std::deque<T> q_;
};