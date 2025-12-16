#!/bin/bash

# 移动 C++ 源代码文件到 src/
echo "📁 组织源代码文件..."
mv -v event_modern.* src/ 2>/dev/null || true
mv -v impl_fake_modern.* src/ 2>/dev/null || true
mv -v impl_msgq_modern.* src/ 2>/dev/null || true
mv -v impl_zmq_modern.* src/ 2>/dev/null || true
mv -v ipc_modern.* src/ 2>/dev/null || true
mv -v msgq_modern.* src/ 2>/dev/null || true
mv -v msgq_tests_modern.cc src/ 2>/dev/null || true
mv -v msgq_examples.cc src/ 2>/dev/null || true
mv -v *.o src/ 2>/dev/null || true

# 移动分析文档到 docs/analysis/
echo "📊 组织分析文档..."
mv -v *_ANALYSIS.md docs/analysis/ 2>/dev/null || true
mv -v *_COMPARISON.md docs/analysis/ 2>/dev/null || true
mv -v CODE_COMPARISON.md docs/analysis/ 2>/dev/null || true
mv -v EVENT_REFACTORING_SUMMARY.md docs/analysis/ 2>/dev/null || true
mv -v event_analysis.md docs/analysis/ 2>/dev/null || true

# 移动迁移指南到 docs/migration-guides/
echo "📖 组织迁移指南..."
mv -v *_MIGRATION_GUIDE.md docs/migration-guides/ 2>/dev/null || true

# 移动总结文档到 docs/summaries/
echo "📋 组织项目总结..."
mv -v PROJECT_COMPLETION_SUMMARY.md docs/summaries/ 2>/dev/null || true
mv -v PROJECT_SUMMARY.txt docs/summaries/ 2>/dev/null || true
mv -v FINAL_SUMMARY.txt docs/summaries/ 2>/dev/null || true
mv -v MIGRATION_COMPLETION_STATUS.md docs/summaries/ 2>/dev/null || true
mv -v MIGRATION_ROADMAP.md docs/summaries/ 2>/dev/null || true
mv -v MODERNIZATION_SUMMARY.md docs/summaries/ 2>/dev/null || true
mv -v REFACTORING_GUIDE.md docs/summaries/ 2>/dev/null || true
mv -v SOURCE_CODE_DELIVERY_SUMMARY.txt docs/summaries/ 2>/dev/null || true
mv -v README_DELIVERY.md docs/summaries/ 2>/dev/null || true
mv -v DELIVERY_CHECKLIST.md docs/summaries/ 2>/dev/null || true

# 移动示例代码到 examples/
echo "💡 组织示例代码..."
mv -v test_compile.sh examples/ 2>/dev/null || true

# 移动 Python 绑定到 bindings/python/
echo "🐍 组织 Python 绑定..."
mv -v __init__.py bindings/python/ 2>/dev/null || true
mv -v ipc.pxd bindings/python/ 2>/dev/null || true
mv -v ipc_pyx.pyx bindings/python/ 2>/dev/null || true

echo "✅ 文件整理完成!"
ls -la
