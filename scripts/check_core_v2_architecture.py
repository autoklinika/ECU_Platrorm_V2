#!/usr/bin/env python3
"""Conservative source/CMake architecture checks, not a C++ conformance proof."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1]).resolve()
failed = False

def reject(path, label):
    global failed
    print(f'FAIL: Core V2 contains forbidden {label}: {path}', file=sys.stderr)
    failed = True

# Model translation-phase line splicing before lexical scanning so a directive
# split with backslash-newline cannot bypass include or forbidden-token checks.
# Comments/literals are then masked. Only a quoted token directly following an
# include directive is retained for include extraction.
lex = re.compile(r'R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
def clean(source):
    source = re.sub(r'\\\r?\n', '', source)
    def mask(match):
        return re.sub(r'[^\n]', ' ', match.group())
    code = lex.sub(mask, source)
    def include_token(match):
        prefix = code[code.rfind('\n', 0, match.start()) + 1:match.start()]
        if match.group().startswith('"') and re.fullmatch(r'\s*#\s*include\s*', prefix):
            return match.group()
        return mask(match)
    include_source = lex.sub(include_token, source)
    include_directives = re.findall(
        r'^\s*#\s*include\b[^\n]*',
        include_source,
        re.M,
    )
    includes = re.findall(
        r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]',
        include_source,
        re.M,
    )
    return code, includes, len(include_directives) != len(includes)

def is_within(path, parent):
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False

allowed_standard_headers = {
    'array',
    'chrono',
    'cstddef',
    'cstdint',
    'limits',
    'string_view',
}

unsafe_headers = {
    'vector', 'string', 'memory', 'memory_resource', 'functional', 'any',
    'future', 'thread', 'map', 'unordered_map', 'unordered_set', 'set',
    'list', 'deque', 'forward_list', 'filesystem', 'sstream', 'iostream',
    'istream', 'ostream', 'fstream', 'streambuf', 'syncstream', 'regex',
    'valarray', 'stack', 'queue', 'scoped_allocator', 'stop_token',
    'atomic', 'mutex', 'shared_mutex', 'condition_variable', 'semaphore',
    'barrier', 'latch',
}

patterns = {
    'preprocessor macro': r'(?m)^\s*#\s*(?:define|undef)\b',
    'preprocessor digraph': r'%:',
    'std namespace import': r'\busing\s+namespace\s+(?:::\s*)?std\b',
    'std namespace alias': r'\bnamespace\s+\w+\s*=\s*(?:::\s*)?std\b',
    'worker/sleep API': r'\bstd\s*::\s*(?:thread|jthread|async|future|shared_future|promise|packaged_task)\b|\b(?:pthread_\w+|thrd_\w+|mtx_\w+|cnd_\w+|tss_\w+|call_once|sleep_for|sleep_until|sleep|usleep|nanosleep)\s*\(',
    'synchronization primitive': r'\bstd\s*::\s*(?:atomic|mutex|recursive_mutex|timed_mutex|recursive_timed_mutex|shared_mutex|shared_timed_mutex|condition_variable|condition_variable_any|counting_semaphore|binary_semaphore|barrier|latch)\b|\bthread_local\b',
    'exception/RTTI facility': r'\b(?:throw|try|catch|dynamic_cast|typeid)\b',
    'unbounded loop': r'\bwhile\s*\(\s*(?:true|1)\s*\)|\bfor\s*\(\s*;\s*;\s*\)',
    'allocation facility': r'\bstd\s*::\s*(?:vector|set|multiset|map|multimap|list|forward_list|deque|unordered_\w+|string|basic_string|function|any|make_any|make_unique|make_shared|allocate_shared|allocator|unique_ptr|shared_ptr|ostringstream|istringstream|stringstream)\b|\bstd\s*::\s*pmr\s*::|\bnew\b|\b(?:malloc|calloc|realloc|aligned_alloc)\s*\(',
    'explicit memory primitive': r'\b(?:(?:std\s*::\s*)?(?:memcpy|memset|memmove|memcmp)|__builtin_(?:memcpy|memset|memmove|memcmp))\s*\(',
    'OS API': r'\bstd\s*::\s*filesystem\b',
    'platform process API': r'\b(?:system|popen|_popen|fork|vfork|exec[lvpe]*|posix_spawn(?:p)?|CreateProcess(?:A|W)?)\b',
    'direct clock API': r'\b(?:steady_clock|system_clock|high_resolution_clock|clock_gettime|gettimeofday|QueryPerformanceCounter|GetTickCount(?:64)?)\b',
    'product/UI dependency': r'\b(?:Qt\w*|QML|WebSocket|HTTP)\b',
}
try:
    files = [p for p in root.rglob('*') if p.is_file() and p.suffix in {'.h','.hh','.hpp','.hxx','.c','.cc','.cpp','.cxx','.ipp','.tpp','.inl'}]
    if not files:
        raise ValueError('No C/C++ Core V2 source files found')
    for path in files:
        code, includes, nonliteral_include = clean(path.read_text())
        if nonliteral_include:
            reject(path, 'macro/non-literal include')
        for label, pattern in patterns.items():
            if re.search(pattern, code):
                reject(path, label)
        for delimiter, inc in includes:
            if inc in unsafe_headers:
                reject(path, 'allocating/worker standard include')
            if re.match(r'(?:linux/|sys/|net/|pthread\.h$|threads\.h$|unistd\.h$|windows\.h$|thread$|future$|filesystem$|ctime$|time\.h$)', inc, re.I):
                reject(path, 'OS/worker/direct-time include')
            if inc.startswith(('ecu/core/', 'ecu/sac/', 'src/ecu/')):
                reject(path, 'legacy/product dependency')

            if delimiter == '"':
                candidates = (
                    (path.parent / inc).resolve(),
                    (root / 'include' / inc).resolve(),
                )
                existing = tuple(candidate for candidate in candidates if candidate.exists())
                if not existing:
                    reject(path, 'unresolved quoted include')
                elif not any(is_within(candidate, root) for candidate in existing):
                    reject(path, 'out-of-root quoted include')
            else:
                if inc.startswith('ecu/core_v2/'):
                    candidate = (root / 'include' / inc).resolve()
                    if not candidate.exists() or not is_within(candidate, root):
                        reject(path, 'unresolved/out-of-root V2 angle include')
                elif inc not in allowed_standard_headers:
                    reject(path, 'unapproved system/external angle include')

            if inc.startswith('ecu/') and not inc.startswith('ecu/core_v2/'):
                reject(path, 'non-V2 ECU include')
    scope = root / 'include/ecu/core_v2/domain/product_scope.hpp'
    code, _, _ = clean(scope.read_text())
    enums = re.findall(r'\benum\s+class\s+MachineDomain\s*:[^{]+\{([^}]+)\}', code)
    if len(enums) != 1 or re.sub(r'\s+', '', enums[0]).rstrip(',') != 'truck=0U,agri=1U,ohv=2U':
        reject(scope, 'product scope (exact TRUCK/AGRI/OHV enum required)')
    for path in root.rglob('*'):
        if path.is_file() and (path.name == 'CMakeLists.txt' or path.suffix == '.cmake'):
            cmake = re.sub(r'#[^\n]*', '', path.read_text())
            # Foundation has no link dependencies. Fail closed rather than
            # trying to resolve arbitrary CMake variables or platform aliases.
            if re.search(r'\b(?:target_link_libraries|link_libraries|link_directories|find_package|pkg_check_modules)\s*\(|(?:-pthread|-l\w+|/DEFAULTLIB:)', cmake, re.I):
                reject(path, 'CMake link/platform dependency')
except (OSError, ValueError) as error:
    print(f'FAIL: Core V2 scanner error: {error}', file=sys.stderr)
    failed = True
print('CORE_V2_ARCHITECTURE_GATE=' + ('FAIL' if failed else 'PASS'))
sys.exit(1 if failed else 0)
