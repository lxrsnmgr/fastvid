#ifndef __SRC_COMMON_CONCURRENCY_BOUNDEDQUEUE_H__
#define __SRC_COMMON_CONCURRENCY_BOUNDEDQUEUE_H__

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace sclient{

//线程安全的有界队列
//支持多生产者-多消费者模式，当队列满时，新元素会替换最旧的元素
//提供阻塞和非阻塞两种出队方式，支持优雅关闭
template <typename T>
class BoundedQueue{
public:
    //构造函数
    //capacity 队列最大容量，最小为1
    explicit BoundedQueue(std::size_t capacity)
        : _capacity(std::max<std::size_t>(1, capacity)),
        _closed(false){

        }

    //插入元素，队列满时丢弃最旧的元素
    //这种策略确保生产者不会被阻塞，适用于实时性要求高的场景
    //@param value 要推入的元素
    //@return 成功推入返回true,队列已关闭返回false
    bool PushOrDropOldest(T value){
        std::lock_guard<std::mutex> lock(_mutex);
        if(_closed){
            return false;
        }

        //队列已满时，移除最旧的元素为新元素腾出空间
        if(_queue.size() >= _capacity){
            _queue.pop_front();
        }

        _queue.push_back(std::move(value));
        _condition.notify_one();
        return true;
    }

    //阻塞等待并弹出队首元素
    //会阻塞当前线程直到有元素可弹出或队列被关闭
    //适用于消费者需要确保处理每个元素的场景
    //@param value 用于存储弹出元素的指针
    //@return 成功弹出返回true，队列为空或已关闭返回false
    bool WaitPop(T *value){
        if(value == nullptr){
            return false;
        }

        std::unique_lock<std::mutex> lock(_mutex);
        //等待直到队列非空或已关闭
        _condition.wait(lock, [this](){
            return _closed || !_queue.empty();
        });
        //队列已关闭且为空时返回失败
        if(_queue.empty()){
            return false;
        }

        *value = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    //非阻塞尝试弹出队首元素
    //立即返回，不阻塞等待，适用于轮询或可选处理的场景
    //@param value 用于存储弹出元素的指针
    //@return 成功弹出返回true，队列为空返回false
    bool TryPop(T *value){
        if(value == nullptr){
            return false;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        if(_queue.empty()){
            return false;
        }

        *value = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    //获取当前队列元素数量
    //注意：在多线程环境下，返回值可能已过时
    std::size_t Size() const{
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

    //关闭队列
    //关闭后，所有等待中的消费者会被唤醒，Push操作将失败
    void Close(){
        std::lock_guard<std::mutex> lock(_mutex);
        _closed = true;
        _condition.notify_all();
    }

private:
          std::size_t _capacity;
          bool _closed;
          std::deque<T> _queue;
          mutable std::mutex _mutex;
          std::condition_variable _condition;
};
}
#endif
