/*
 * Example: Using std::span with msgq_modern.h
 * Demonstrates both C++17 and C++20 span compatibility
 * Compile with: g++ -std=c++20 -I./src -O2 span_examples.cc
 */

#include "msgq_modern.h"
#include <iostream>
#include <array>
#include <vector>

void example_cpp17_span() {
    std::cout << "\n=== Example 1: C++17 Compatible Spans ===\n";
    
    std::vector<char> data = {'H', 'e', 'l', 'l', 'o'};
    auto span1 = msgq::make_span(data);
    
    std::cout << "Span from vector:\n";
    std::cout << "  Size: " << span1.size() << "\n";
    std::cout << "  Data: ";
    for (size_t i = 0; i < span1.size(); ++i) std::cout << span1[i];
    std::cout << "\n";
    
    auto span2 = msgq::make_span(data.data(), data.size());
    std::cout << "Span from pointer & size: " << span2.size() << " bytes\n";
    
    std::array<int, 4> arr = {1, 2, 3, 4};
    auto span3 = msgq::make_span(arr);
    std::cout << "Span from array: " << span3.size() << " elements\n";
}

void example_span_containers() {
    std::cout << "\n=== Example 2: Span with Different Containers ===\n";
    
    std::vector<char> vec = {1, 2, 3};
    auto vec_span = msgq::make_span(vec);
    std::cout << "Vector span: " << vec_span.size() << " elements\n";
    
    std::array<char, 5> arr = {5, 4, 3, 2, 1};
    auto arr_span = msgq::make_span(arr);
    std::cout << "Array span: " << arr_span.size() << " elements\n";
    
    char buffer[10] = {'H', 'e', 'l', 'l', 'o'};
    auto buf_span = msgq::make_span(buffer, 5);
    std::cout << "Buffer span: " << buf_span.size() << " elements\n";
}

void example_span_access() {
    std::cout << "\n=== Example 3: Span Element Access ===\n";
    
    std::vector<int> data = {10, 20, 30, 40, 50};
    auto span = msgq::make_span(data);
    
    std::cout << "Element access:\n";
    std::cout << "  Size: " << span.size() << "\n";
    std::cout << "  Empty: " << (span.empty() ? "yes" : "no") << "\n";
    std::cout << "  First: " << span.front() << "\n";
    std::cout << "  Last: " << span.back() << "\n";
    std::cout << "  [2]: " << span[2] << "\n";
}

void example_message_span() {
    std::cout << "\n=== Example 4: Message with Span ===\n";
    
    std::vector<char> data = {'M', 's', 'g', 'Q'};
    auto span = msgq::make_span(data);
    
    msgq::Message msg(span);
    
    std::cout << "Message created from span:\n";
    std::cout << "  Size: " << msg.size() << " bytes\n";
    
    auto msg_span = msg.data();
    std::cout << "  Content: ";
    for (size_t i = 0; i < msg_span.size(); ++i) std::cout << msg_span[i];
    std::cout << "\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  msgq_modern.h - std::span Examples               ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n";
    
    try {
        example_cpp17_span();
        example_span_containers();
        example_span_access();
        example_message_span();
        
        std::cout << "\n✅ All examples completed successfully!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }
}
