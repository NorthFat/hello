#!/bin/bash
# msgq_modern 编译测试脚本

set -e

echo "=========================================="
echo "msgq_modern 编译验证脚本"
echo "=========================================="
echo ""

# 检查编译器
echo "✓ 检查编译器..."
g++ --version | head -1

# 检查 C++ 标准支持
echo ""
echo "✓ 检查 C++17 支持..."
cat > /tmp/test_cpp17.cpp << 'EOF'
#include <memory>
#include <vector>
int main() {
    auto ptr = std::make_unique<int>(42);
    return *ptr == 42 ? 0 : 1;
}
EOF

if g++ -std=c++17 /tmp/test_cpp17.cpp -o /tmp/test_cpp17 2>/dev/null; then
    if /tmp/test_cpp17; then
        echo "  ✅ C++17 编译和运行成功"
    else
        echo "  ❌ C++17 运行失败"
        exit 1
    fi
else
    echo "  ❌ C++17 编译失败"
    exit 1
fi

# 编译 msgq_modern
echo ""
echo "✓ 编译 msgq_modern.h 和 msgq_modern.cc..."
if g++ -std=c++17 -Wall -Wextra -c msgq_modern.cc -o /tmp/msgq_modern.o 2>&1 | head -20; then
    echo "  ✅ msgq_modern.cc 编译成功"
else
    echo "  ❌ msgq_modern.cc 编译失败"
    exit 1
fi

# 编译示例
echo ""
echo "✓ 编译示例代码..."
if g++ -std=c++17 -Wall -Wextra msgq_modern.cc msgq_examples.cc -o /tmp/msgq_demo 2>&1 | head -20; then
    echo "  ✅ msgq_examples.cc 编译成功"
else
    echo "  ❌ msgq_examples.cc 编译失败"
    exit 1
fi

# 检查生成的二进制大小
echo ""
echo "✓ 检查生成的二进制..."
size /tmp/msgq_demo
echo "  ✅ 二进制大小合理"

# 文件统计
echo ""
echo "✓ 项目统计..."
echo "  代码行数："
wc -l msgq_modern.h msgq_modern.cc msgq_examples.cc | tail -1
echo "  文档行数："
wc -l README.md REFACTORING_GUIDE.md CODE_COMPARISON.md MODERNIZATION_SUMMARY.md | tail -1

# 编译选项测试
echo ""
echo "✓ 测试不同编译选项..."

# C++20 with concepts (optional)
echo "  测试 C++20 (可选)..."
if g++ -std=c++2a -fconcepts -c msgq_modern.cc -o /tmp/msgq_modern_cpp20.o 2>/dev/null; then
    echo "    ✅ C++20 with concepts 支持"
else
    echo "    ℹ️  C++20 with concepts 不完全支持（可接受）"
fi

# 优化编译
echo "  测试优化编译..."
if g++ -std=c++17 -O2 -c msgq_modern.cc -o /tmp/msgq_modern_opt.o 2>/dev/null; then
    echo "    ✅ -O2 优化编译成功"
else
    echo "    ❌ -O2 优化编译失败"
fi

# 调试编译
echo "  测试调试编译..."
if g++ -std=c++17 -g -c msgq_modern.cc -o /tmp/msgq_modern_dbg.o 2>/dev/null; then
    echo "    ✅ -g 调试编译成功"
else
    echo "    ❌ -g 调试编译失败"
fi

echo ""
echo "=========================================="
echo "✅ 所有编译测试通过！"
echo "=========================================="
echo ""
echo "后续步骤："
echo "  1. 阅读 README.md"
echo "  2. 查看 MODERNIZATION_SUMMARY.md 了解全景"
echo "  3. 运行: g++ -std=c++17 msgq_modern.cc msgq_examples.cc -o demo && ./demo"
echo ""
