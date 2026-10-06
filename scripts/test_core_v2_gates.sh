#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT

for extension in h hh hpp hxx c cc cpp cxx ipp tpp inl; do
  for construct in \
    '#include <sys/ioctl.h>' \
    '#include </usr/include/unistd.h>' \
    '#include <../../core/include/ecu/core/build_info.hpp>' \
    '#include <boost/asio.hpp>' \
    '#include <pthread.h>' \
    '#include <threads.h>' \
    '#include <ctime>' \
    '#include <time.h>' \
    '#include <future>' \
    '#include <mutex>' \
    '#include <atomic>' \
    '#include <condition_variable>' \
    '#include <vector>' \
    '#include "memory"' \
    '#include <string>' \
    '#include <functional>' \
    '#include <unordered_set>' \
    'using namespace std; vector<int> values;' \
    'namespace s = std; s::jthread worker;' \
    'using std::vector; vector<int> values;' \
    'using std::jthread; jthread worker;' \
    'std::async(work);' \
    'std::future<int> value;' \
    'std::shared_future<int> value;' \
    'std::promise<int> value;' \
    'std::packaged_task<void()> value;' \
    'std::mutex value;' \
    'std::atomic<int> value;' \
    'thread_local int value;' \
    'throw 1;' \
    'try { work(); } catch (...) {}' \
    'dynamic_cast<void*>(ptr);' \
    'typeid(value);' \
    'while (true) { work(); }' \
    'for (;;) { work(); }' \
    'thrd_create(worker, callback, 0);' \
    'std::allocate_shared<int>(allocator);' \
    'memcpy(dst, src, 8);' \
    'std::memset(dst, 0, 8);' \
    '__builtin_memmove(dst, src, 8);' \
    'std::any value;' \
    'std::make_unique<int>();' \
    'std::make_shared<int>();' \
    'std::vector<int> values;' \
    'std::string value;' \
    'std::basic_string<char> value;' \
    'std::function<void()> value;' \
    'std::ostringstream value;' \
    'std::istringstream value;' \
    'std::stringstream value;' \
    'std::allocator<int> value;' \
    'std::pmr::memory_resource* value;' \
    'std::unique_ptr<int> value;' \
    'std::shared_ptr<int> value;' \
    'auto value = new int;' \
    'auto value = new int[8];' \
    'malloc(8);' \
    'calloc(8, 1);' \
    'realloc(value, 8);' \
    'std::system("true");' \
    'using std::system; void f() { system("true"); }' \
    'std::chrono::steady_clock::now();' \
    'using Clock = std::chrono::steady_clock; void f() { (void)Clock::now(); }' \
    '%:define HIDDEN_CLOCK steady_clock' \
    '#include "../legacy.hpp"' \
    '#include "ecu/core/legacy.hpp"'
  do
    printf '%s\n' "$construct" > "$fixture/forbidden.$extension"
    if ECU_CORE_V2_SCAN_ROOT="$fixture" \
      bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh" \
      > "$fixture/log" 2>&1
    then
      echo "FAIL: architecture scan missed $construct in .$extension" >&2
      exit 1
    fi
    if ! grep -q 'FAIL: Core V2 contains forbidden' "$fixture/log"; then
      cat "$fixture/log"
      exit 1
    fi
    rm "$fixture/forbidden.$extension"
  done
done

# Translation-phase line splicing must not hide an out-of-root include.
printf '%s\n' '#include \' '  "../legacy.hpp"' > "$fixture/forbidden.cpp"
if ECU_CORE_V2_SCAN_ROOT="$fixture" \
  bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh" \
  > "$fixture/log" 2>&1
then
  echo 'FAIL: architecture scan missed line-spliced relative include' >&2
  exit 1
fi
rm "$fixture/forbidden.cpp"

# Macro-expanded includes are intentionally forbidden because the source scanner
# cannot prove their resolved dependency boundary.
printf '%s\n' \
  '#define CORE_HEADER "ecu/core_v2/domain/product_scope.hpp"' \
  '#include CORE_HEADER' > "$fixture/forbidden.cpp"
if ECU_CORE_V2_SCAN_ROOT="$fixture" \
  bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh" \
  > "$fixture/log" 2>&1
then
  echo 'FAIL: architecture scan accepted macro include' >&2
  exit 1
fi
rm "$fixture/forbidden.cpp"

# Positive fixture includes misleading comments and legitimate bounded names.
mkdir -p "$fixture/include/ecu/core_v2/domain"
cp "$ROOT_DIR/src/core_v2/include/ecu/core_v2/domain/product_scope.hpp" "$fixture/include/ecu/core_v2/domain/"
printf '%s\n' '// std::async std::string new pthread_create()' '/* #include <threads.h> */' '#include <array>' '#include <chrono>' '#include <cstdint>' '#include <cstddef>' '#include <string_view>' 'std::string_view view; int renewal;' 'const char* text = "using namespace std; namespace s = std; std::vector<int> v; #include <memory>";' 'const char* raw = R"tag(using std::jthread;' '#include <vector>' ')tag";' '// namespace s = std; using namespace std;' > "$fixture/allowed.cpp"
ECU_CORE_V2_SCAN_ROOT="$fixture" bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh"
for construct in 'target_link_libraries(ecu_core_v2 PRIVATE Threads::Threads)' 'target_link_libraries(ecu_core_v2 PRIVATE ws2_32)' 'find_package(Threads REQUIRED)'; do
  printf '%s\n' "$construct" > "$fixture/CMakeLists.txt"
  if ECU_CORE_V2_SCAN_ROOT="$fixture" bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh" > "$fixture/log" 2>&1; then
    echo "FAIL: missed CMake dependency $construct" >&2; exit 1
  fi
done
rm "$fixture/CMakeLists.txt"
sed -i '/ohv = 2U,/a\  marine = 3U,' "$fixture/include/ecu/core_v2/domain/product_scope.hpp"
if ECU_CORE_V2_SCAN_ROOT="$fixture" bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh" > "$fixture/log" 2>&1; then
  echo 'FAIL: fourth domain accepted' >&2; exit 1
fi
# Scanner execution errors must fail closed.
mkdir -p "$fixture/bin"
printf '%s\n' '#!/usr/bin/env bash' 'exit 2' > "$fixture/bin/python3"
chmod +x "$fixture/bin/python3"
if PATH="$fixture/bin:$PATH" ECU_CORE_V2_SCAN_ROOT="$fixture" bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh" > "$fixture/log" 2>&1; then
  echo 'FAIL: scanner error accepted' >&2; exit 1
fi
grep -q 'CORE_V2_ARCHITECTURE_GATE=FAIL' "$fixture/log"

printf '%s\n' '#include <sys/ioctl.h>' > "$fixture/forbidden.h"
if ECU_CORE_PORTABILITY_ROOT="$fixture" \
  bash "$ROOT_DIR/scripts/check_core_portability.sh" \
  > "$fixture/log" 2>&1
then
  echo 'FAIL: portability scan missed sys/ioctl' >&2
  exit 1
fi

# A default-on Linux-only adapter must not force legacy Core on non-Linux
# V2-only builds where the adapter is not part of the build graph.
cmake -S "$ROOT_DIR" -B "$fixture/nonlinux-v2-only" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Generic \
  -DECU_BUILD_TESTS=OFF \
  -DECU_BUILD_LEGACY_CORE=OFF \
  -DECU_BUILD_CORE_V2=ON \
  -DECU_BUILD_SAC_MODULE=OFF \
  -DECU_BUILD_LINUX_SOCKETCAN=ON \
  > "$fixture/nonlinux-configure.log"

echo 'CORE_V2_GATE_NEGATIVE_TESTS=PASS'
