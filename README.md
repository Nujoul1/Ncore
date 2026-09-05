# Ncore

Ncore 是一个小型 C 基础工具库，用来集中维护项目中可复用的通用模块。

目前包含：

- `nfifo`：通用 FIFO
- `nthread_message`：基于 FIFO、mutex 和 condition variable 的线程消息队列
- `nlog`：轻量日志接口

## 构建

需要 CMake 3.16 或更高版本。

```bash
cmake -S . -B build
cmake --build build
```

Ncore 单独构建时默认会同时构建测试：

```bash
ctest --test-dir build --output-on-failure
```

如果不需要测试：

```bash
cmake -S . -B build -DNCORE_BUILD_TESTS=OFF
```

## 在其他 CMake 项目中使用

把 Ncore 源码放到工程目录中，例如：

```text
my_project/
├── CMakeLists.txt
├── src/
└── third_party/
    └── Ncore/
```

然后在主工程的 `CMakeLists.txt` 中加入：

```cmake
add_subdirectory(third_party/Ncore)

target_link_libraries(my_app PRIVATE Ncore::ncore)
```

代码中直接包含需要的头文件：

```c
#include <ncore/nfifo.h>
#include <ncore/nthread_message.h>
#include <ncore/nlog.h>
```

Ncore 作为子目录加入其他工程时，默认不会构建自己的测试。
