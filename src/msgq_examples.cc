#include "msgq_modern.h"
#include <iostream>
#include <thread>
#include <chrono>

// ============================================================================
// 示例 1：基本的发送/接收
// ============================================================================

void example_basic() {
    std::cout << "\n=== Example 1: Basic Send/Receive ===" << std::endl;
    
    try {
        // 创建发布者队列
        auto pub_queue = msgq::Queue::create("example1", 10 * 1024 * 1024);
        pub_queue.init_publisher();
        
        // 创建订阅者队列（同名）
        auto sub_queue = msgq::Queue::create("example1", 10 * 1024 * 1024);
        sub_queue.init_subscriber();
        
        // 发送消息
        std::string msg_data = "Hello from Publisher!";
        pub_queue.send(gsl::span<const char>(msg_data.data(), msg_data.size()));
        std::cout << "Publisher sent: " << msg_data << std::endl;
        
        // 接收消息
        auto received = sub_queue.recv(1000);  // 1 秒超时
        if (!received.empty()) {
            std::cout << "Subscriber received: ";
            std::cout.write(received.data().data(), received.size());
            std::cout << std::endl;
        }
        
    }  // ✅ 自动清理，无需手动调用 msgq_close_queue
    catch (const msgq::MessageQueueError& e) {
        std::cerr << "Queue error: " << e.what() << std::endl;
    }
}

// ============================================================================
// 示例 2：使用 Message 对象
// ============================================================================

void example_message_object() {
    std::cout << "\n=== Example 2: Message Object ===" << std::endl;
    
    try {
        auto pub_queue = msgq::Queue::create("example2", 10 * 1024 * 1024);
        pub_queue.init_publisher();
        
        auto sub_queue = msgq::Queue::create("example2", 10 * 1024 * 1024);
        sub_queue.init_subscriber();
        
        // 创建 Message 对象 - RAII 管理
        msgq::Message msg(256);
        
        // 填充数据
        std::string data = "Message data";
        std::copy(data.begin(), data.end(), msg.data().begin());
        
        // 发送
        pub_queue.send(msg);
        std::cout << "Sent message of size: " << msg.size() << std::endl;
        
        // 接收
        auto received = sub_queue.recv(1000);
        std::cout << "Received message of size: " << received.size() << std::endl;
        
    }  // ✅ msg 和 queue 都自动清理
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

// ============================================================================
// 示例 3：异常安全性
// ============================================================================

void example_exception_safety() {
    std::cout << "\n=== Example 3: Exception Safety ===" << std::endl;
    
    try {
        auto queue = msgq::Queue::create("example3", 10 * 1024 * 1024);
        queue.init_publisher();
        
        // 尝试发送过大的消息
        msgq::Message large_msg(10 * 1024 * 1024);  // 10MB (改小以便测试)
        queue.send(large_msg);
        
    }  // ✅ 即使异常被抛出，资源也自动清理
    catch (const std::out_of_range& e) {
        std::cerr << "Message too large: " << e.what() << std::endl;
    }
    catch (const msgq::MessageQueueError& e) {
        std::cerr << "Queue error: " << e.what() << std::endl;
    }
    
    std::cout << "After exception, all resources cleaned up automatically" << std::endl;
}

// ============================================================================
// 示例 4：线程安全
// ============================================================================

void example_multithreaded() {
    std::cout << "\n=== Example 4: Multithreaded ===" << std::endl;
    
    try {
        auto pub_queue = msgq::Queue::create("example4", 10 * 1024 * 1024);
        pub_queue.init_publisher();
        
        auto sub_queue1 = msgq::Queue::create("example4", 10 * 1024 * 1024);
        sub_queue1.init_subscriber();
        
        auto sub_queue2 = msgq::Queue::create("example4", 10 * 1024 * 1024);
        sub_queue2.init_subscriber();
        
        // 发布者线程
        std::thread publisher([&pub_queue]() {
            for (int i = 0; i < 5; ++i) {
                std::string msg = "Message " + std::to_string(i);
                pub_queue.send(gsl::span<const char>(msg.data(), msg.size()));
                std::cout << "[Publisher] Sent: " << msg << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        
        // 订阅者线程 1
        std::thread subscriber1([&sub_queue1]() {
            for (int i = 0; i < 5; ++i) {
                auto msg = sub_queue1.recv(1000);
                std::cout << "[Subscriber1] Received: ";
                std::cout.write(msg.data().data(), msg.size());
                std::cout << std::endl;
            }
        });
        
        // 订阅者线程 2
        std::thread subscriber2([&sub_queue2]() {
            for (int i = 0; i < 5; ++i) {
                auto msg = sub_queue2.recv(1000);
                std::cout << "[Subscriber2] Received: ";
                std::cout.write(msg.data().data(), msg.size());
                std::cout << std::endl;
            }
        });
        
        publisher.join();
        subscriber1.join();
        subscriber2.join();
        
    }  // ✅ 所有线程完成后自动清理
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

// ============================================================================
// 示例 5：现代 C++ 特性 - constexpr
// ============================================================================

void example_constexpr() {
    std::cout << "\n=== Example 5: Constexpr Features ===" << std::endl;
    
    // 在编译期创建 PackedPointer
    constexpr msgq::PackedPointer initial(1, 100);
    constexpr uint32_t init_cycle = initial.cycle();
    constexpr uint32_t init_offset = initial.offset();
    
    std::cout << "Initial cycle (compile-time): " << init_cycle << std::endl;
    std::cout << "Initial offset (compile-time): " << init_offset << std::endl;
    
    // 编译时验证
    static_assert(initial.cycle() == 1);
    static_assert(initial.offset() == 100);
    
    std::cout << "Compile-time checks passed" << std::endl;
}

// ============================================================================
// 示例 6：RAII 保证
// ============================================================================

void example_raii_guarantee() {
    std::cout << "\n=== Example 6: RAII Guarantee ===" << std::endl;
    
    // 自定义作用域演示
    {
        std::cout << "Creating queue..." << std::endl;
        auto queue = msgq::Queue::create("example6", 10 * 1024 * 1024);
        queue.init_publisher();
        
        std::cout << "Queue created, in scope" << std::endl;
        
        // 即使异常发生，也会销毁
        if (true) {  // 可以改为 if(false) 测试
            std::cout << "Queue destroyed when going out of scope" << std::endl;
        }
    }  // ← 这里自动调用 ~Queue，释放所有资源
    
    std::cout << "Resources cleaned up" << std::endl;
}

// ============================================================================
// 示例 7：与 std 容器的集成
// ============================================================================

void example_std_integration() {
    std::cout << "\n=== Example 7: STL Integration ===" << std::endl;
    
    try {
        auto pub_queue = msgq::Queue::create("example7", 10 * 1024 * 1024);
        pub_queue.init_publisher();
        
        // 使用 std::vector 创建消息
        std::vector<int> data = {1, 2, 3, 4, 5};
        auto* raw_data = reinterpret_cast<const char*>(data.data());
        size_t raw_size = data.size() * sizeof(int);
        
        // 发送
        pub_queue.send(gsl::span<const char>(raw_data, raw_size));
        
        // 使用 Message 对象存储 vector 数据
        msgq::Message msg;
        msg.resize(raw_size);
        std::memcpy(msg.data().data(), raw_data, raw_size);
        
        std::cout << "Message integration with STL successful" << std::endl;
        
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "msgq_modern.h - Modern C++ Examples" << std::endl;
    std::cout << "====================================" << std::endl;
    
    try {
        // 依次运行示例
        example_basic();
        example_message_object();
        example_exception_safety();
        example_constexpr();
        example_raii_guarantee();
        example_std_integration();
        
        // 多线程示例（可选，因为较耗时）
        // example_multithreaded();
        
        std::cout << "\n====================================" << std::endl;
        std::cout << "All examples completed successfully!" << std::endl;
        
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
