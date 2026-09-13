#!/bin/sh

# Keep docs/sim.md tied to the simulator vocabulary. This is deliberately a
# small source-text gate. It does not need a build or network access.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec python3 - "$ROOT" "$@" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
check_links = "--links" in sys.argv[2:]
self_test = "--self-test" in sys.argv[2:]
docs_path = root / "docs" / "sim.md"
docs = docs_path.read_text(encoding="utf-8")
action_source = (root / "sim" / "scenario_action.cpp").read_text(encoding="utf-8")
driver_source = (root / "sim" / "driver.cpp").read_text(encoding="utf-8")
sources = "\n".join(
    (root / path).read_text(encoding="utf-8", errors="replace")
    for path in (
        "sim/driver.cpp",
        "sim/scenario_action.cpp",
        "src/FurbleUI.cpp",
        "sim/main.cpp",
        "tests/host",
    )
    if (root / path).is_file()
)
if root.joinpath("tests/host").is_dir():
    sources += "\n" + "\n".join(
        p.read_text(encoding="utf-8", errors="replace")
        for p in root.joinpath("tests/host").rglob("*")
        if p.is_file()
    )

missing = []

def source_values(text, pattern, label):
    matches = list(re.finditer(pattern, text, re.DOTALL))
    if len(matches) != 1:
        raise ValueError(f"action parser inventory {label}: expected one list, got {len(matches)}")
    body = matches[0].group(1)
    body_without_strings = re.sub(r'"[^"\n]*"', "", body)
    if re.sub(r"[\s,]", "", body_without_strings):
        raise ValueError(f"action parser inventory {label}: unsupported list expression")
    return re.findall(r'"([^"\n]+)"', body)

def function_source(text, function_name):
    match = re.search(rf"\b{function_name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        return None
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():index + 1]
    raise ValueError(f"unterminated {function_name} body")

def brace_block(text, open_index):
    depth = 0
    quoted = False
    escaped = False
    for index in range(open_index, len(text)):
        char = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:index]
    raise ValueError("unterminated validation block")

def validated_gps_values(text, function_name, named_branch=True):
    function = function_source(text, function_name)
    if function is None:
        return []
    if named_branch:
        branch = re.search(
            r'\b(?:if|else if)\s*\(\s*name\s*==\s*"gps_uart_mode"\s*\)\s*\{', function)
        if branch is None:
            raise ValueError(f"{function_name} gps_uart_mode branch is missing")
        body = brace_block(function, branch.end() - 1)
    else:
        body = brace_block(function, function.find("{"))
    if len(re.findall(r'\bif\s*\(', body)) != 1:
        raise ValueError(f"{function_name} GPS value predicate count is not canonical")
    condition = re.search(r'\bif\s*\(\s*([^()]*)\s*\)\s*\{', body, re.DOTALL)
    if condition is None:
        raise ValueError(f"{function_name} GPS value predicate is missing")
    if body[:condition.start()].strip():
        raise ValueError(f"{function_name} GPS value predicate is not the first operation")
    predicate = " ".join(condition.group(1).split())
    if not re.fullmatch(r'value\s*!=\s*"[^"]+"(?:\s*&&\s*value\s*!=\s*"[^"]+")*', predicate):
        raise ValueError(f"{function_name} GPS value predicate has an unsupported shape")
    values = re.findall(r'value\s*!=\s*"([^"]+)"', predicate)
    if not values:
        raise ValueError(f"{function_name} GPS value inventory is empty")
    return values

def gps_missing(gps_docs_text, text, function_name, named_branch=True):
    return [value for value in validated_gps_values(text, function_name, named_branch)
            if f"`{value}`" not in gps_docs_text]

def balanced_condition(text, open_index):
    depth = 0
    quoted = False
    escaped = False
    for index in range(open_index, len(text)):
        char = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:index]
    raise ValueError("unterminated validateSeed predicate")

def validate_seed_predicates(seed_text):
    for match in re.finditer(r'\bif\s*\(', seed_text):
        predicate = " ".join(balanced_condition(seed_text, seed_text.find("(", match.start())).split())
        if re.fullmatch(r'name\s*==\s*"[^"]+"(?:\s*\|\|\s*name\s*==\s*"[^"]+")*', predicate):
            continue
        if re.fullmatch(
            r'std::find\(std::begin\((byteSeeds|booleanSeeds|intervalSeeds)\), '
            r'std::end\(\1\), name\) != std::end\(\1\)', predicate):
            continue
        if predicate in {
            "parseUnsigned(value) > std::numeric_limits<uint8_t>::max()",
            "parseUnsigned(value) > std::numeric_limits<uint16_t>::max()",
            "parseUnsigned(value) > 100",
        }:
            continue
        # Negated validator calls on the seed value. Each one is listed by name
        # rather than matched by shape, so a new validator is a deliberate edit
        # here and gets read once.
        if predicate in {
            "!booleanSeedValue(value)",
            "!bleTopologyIsValid(value)",
            "!intervalSeedIsValid(value)",
        }:
            continue
        if re.fullmatch(
            r'value\s*!=\s*"[^"]+"(?:\s*&&\s*value\s*!=\s*"[^"]+")+', predicate):
            continue
        raise ValueError(f"unrecognized validateSeed predicate: {predicate}")

def seed_contract(seed_text):
    body = seed_text[seed_text.find("{") + 1:]
    names = set()
    for array_name in ("byteSeeds", "booleanSeeds", "intervalSeeds"):
        names.update(source_values(
            body,
            rf"(?:constexpr\s+)?const\s+char\s*\*\s*{array_name}\[\]\s*=\s*\{{(.*?)\s*\}};",
            array_name))

    # Every direct name comparison must use a literal. This catches compound
    # conditions such as clock_ms || liveness_grace_ms, not just their first
    # operand, and rejects a new comparison shape the inventory cannot see.
    comparison = re.compile(r'\bname\s*(==|!=)\s*("[^"]*"|[^\s)&|]+)')
    for match in comparison.finditer(body):
        literal = match.group(2)
        if match.group(1) != "==" or not (literal.startswith('"') and literal.endswith('"')):
            raise ValueError("unrecognized nonliteral validateSeed name comparison")
        names.add(literal[1:-1])
    unsupported = re.search(r'\bname\s*(?:\.|\[)', body)
    if unsupported:
        raise ValueError("unrecognized validateSeed name expression")
    unsupported_value = re.search(r'\bvalue\s*(?:\.|\[|=\s*(?!=))', body)
    if unsupported_value:
        raise ValueError("unrecognized validateSeed value expression")
    value_comparison = re.compile(
        r'\bvalue\s*(==|!=|<=|>=|<(?![=<])|>(?![=>]))\s*("[^"]*"|[^\s)&|]+)')
    for match in value_comparison.finditer(body):
        literal = match.group(2)
        if match.group(1) != "!=" or not (literal.startswith('"') and literal.endswith('"')):
            raise ValueError("unrecognized validateSeed value comparison")
    for call in re.finditer(r'(?<![\w:])([A-Za-z_]\w*)\s*\([^)]*\bvalue\b', body):
        if call.group(1) not in {"if", "parseUnsigned", "batteryVoltage", "parseSigned",
                                 "booleanSeedValue", "bleTopologyIsValid"}:
            raise ValueError(f"unrecognized validateSeed value call: {call.group(1)}")
    for call in re.finditer(r'(?<![\w:])([A-Za-z_]\w*)\s*\([^)]*\bname\b', body):
        if call.group(1) not in {"find", "if"}:
            raise ValueError(f"unrecognized validateSeed name call: {call.group(1)}")
    if re.search(r'\bif\s*\(\s*(?:name|value|[A-Za-z_]\w*)\s*\)', body):
        raise ValueError("unrecognized validateSeed bare predicate")
    validate_seed_predicates(body)
    validated_gps_values(seed_text, "validateSeed")
    if not names:
        raise ValueError("validateSeed inventory is empty")
    return names

def seed_missing(seed_docs_text, seed_text):
    return [name for name in sorted(seed_contract(seed_text)) if f"`{name}`" not in seed_docs_text]

if self_test:
    test_source = '''
void validateSeed(const std::string &name, const std::string &value) {
  if (name == "clock_ms" || name == "liveness_grace_ms") {
    return;
  }
  constexpr const char *byteSeeds[] = {"brightness"};
  constexpr const char *booleanSeeds[] = {"imu"};
  constexpr const char *intervalSeeds[] = {"interval_count"};
  if (std::find(std::begin(byteSeeds), std::end(byteSeeds), name) != std::end(byteSeeds)) {
    if (parseUnsigned(value) > std::numeric_limits<uint8_t>::max()) {
      return;
    }
  }
  if (std::find(std::begin(intervalSeeds), std::end(intervalSeeds), name)
      != std::end(intervalSeeds)) {
    if (parseUnsigned(value) > std::numeric_limits<uint16_t>::max()) {
      return;
    }
  }
  if (name == "watchdog") {
    return;
  }
  if (name == "battery_level") {
    if (parseUnsigned(value) > 100) {
      return;
    }
  }
  if (name == "gps_uart_mode") {
    if (value != "ack" && value != "pause") {
      return;
    }
  }
}
'''
    test_names = seed_contract(test_source)
    assert "liveness_grace_ms" in test_names
    test_docs = " ".join(f"`{name}`" for name in test_names)
    for name in test_names:
        without = test_docs.replace(f"`{name}`", "", 1)
        assert name in seed_missing(without, test_source)
    unsupported_seed_sources = (
        test_source.replace('name == "clock_ms"', 'name.starts_with("clock_ms")'),
        test_source.replace('if (name == "watchdog") {', 'if (name == "watchdog" && extraPredicate) {'),
        test_source.replace('if (name == "watchdog") {', 'if (otherName == "watchdog") {'),
        test_source.replace('value != "ack"', 'value == "ack"'),
        test_source.replace('value != "ack"', 'isAllowed(value) && value != "ack"'),
        test_source.replace('{"brightness"}', '{"brightness", extraSeed}'),
    )
    for unsupported in unsupported_seed_sources:
        try:
            seed_contract(unsupported)
        except ValueError:
            pass
        else:
            raise AssertionError("unknown validateSeed expression was accepted")
    numeric_predicates = re.findall(
        r'parseUnsigned\(value\)\s*>\s*(?:std::numeric_limits<\w+>::max\(\)|100)',
        test_source)
    assert len(numeric_predicates) == 3
    for numeric in numeric_predicates:
        for replacement in (
            f'{numeric} || name == "undocumented_seed"',
            f'name == "hidden_seed" || {numeric}',
            numeric.replace(" > ", " < "),
            numeric.replace("parseUnsigned", "std::parseUnsigned"),
        ):
            unsupported = test_source.replace(numeric, replacement, 1)
            try:
                seed_contract(unsupported)
            except ValueError:
                pass
            else:
                raise AssertionError("unsupported numeric validateSeed predicate was accepted")
    try:
        seed_contract(test_source.replace('intervalSeeds[] = {"interval_count"};\n', ''))
    except ValueError:
        pass
    else:
        raise AssertionError("missing seed list was accepted")
    test_gps_values = validated_gps_values(test_source, "validateSeed")
    test_gps_docs = " ".join(f"`{value}`" for value in test_gps_values)
    for value in test_gps_values:
        without = test_gps_docs.replace(f"`{value}`", "", 1)
        assert value in gps_missing(without, test_source, "validateSeed")
    gps_predicate = '''if (value != "ack" && value != "pause") {
      return;
    }'''
    relocated_gps = test_source.replace(gps_predicate, "") + gps_predicate
    for malformed_gps in (
        relocated_gps,
        test_source.replace(gps_predicate, "return;\n  " + gps_predicate),
    ):
        try:
            seed_contract(malformed_gps)
        except ValueError:
            pass
        else:
            raise AssertionError("relocated or early GPS predicate was accepted")
    test_gps_function = '''
bool validateGpsValue(const std::string &value) {
  if (value != "ack" && value != "pause") {
    return false;
  }
  return true;
}
'''
    assert validated_gps_values(test_gps_function, "validateGpsValue", False) == ["ack", "pause"]
    for unsupported in (
        test_gps_function.replace('value != "pause"', 'value == "pause"'),
        test_gps_function.replace('value != "pause"', 'isAllowed(value) && value != "pause"'),
        test_gps_function.replace('value != "pause"', 'value.starts_with("pause")'),
    ):
        try:
            validated_gps_values(unsupported, "validateGpsValue", False)
        except ValueError:
            pass
        else:
            raise AssertionError("unknown GPS validation expression was accepted")
    print("sim documentation token self-tests passed")
    raise SystemExit(0)

# Fixed action lines are the command vocabulary. Parameter placeholders such as
# `drop N` validate against the command prefix.
action_start = docs.index("### `action` commands")
action_end = docs.index("The battery action", action_start)
action_docs = docs[action_start:action_end]
for command in re.findall(r"^action ([^`\n]+)$", action_docs, re.MULTILINE):
    token = command.split()[0]
    if token not in sources:
        missing.append(f"action {command}")

simple_values = source_values(
    action_source, r"const std::initializer_list<const char \*> simple = \{(.*?)\n  \};", "simple")
for value in simple_values:
    if not re.search(rf"^action\s+{re.escape(value)}(?:\s|$)", action_docs, re.MULTILINE):
        missing.append(f"action {value}")

toggle_values = source_values(
    action_source, r'if \(args\[0\] == "toggle"\).*?oneOf\(args\[1\], \{(.*?)\}\)', "toggle")
for value in toggle_values:
    if f"`{value}`" not in action_docs:
        missing.append(f"toggle {value}")

nav_values = source_values(
    action_source, r"const std::initializer_list<const char \*> pages = \{(.*?)\n  \};", "nav")
for value in nav_values:
    if f"`{value}`" not in action_docs:
        missing.append(f"nav {value}")

page_values = source_values(
    action_source, r"const std::initializer_list<const char \*> pageActions = \{(.*?)\n    \};", "page")
for value in page_values:
    if f"`{value}`" not in action_docs:
        missing.append(f"page {value}")

seed_start = docs.index("### Effective `seed` names")
seed_end = docs.index("### `action` commands", seed_start)
seed_docs = docs[seed_start:seed_end]
seed_source = re.search(r"void validateSeed\(.*?\n\}", sources, re.DOTALL)
if seed_source is None:
    missing.append("seed parser inventory validateSeed")
else:
    seed_text = seed_source.group(0)
    try:
        for name in seed_missing(seed_docs, seed_text):
            missing.append(f"seed {name}")
        for value in gps_missing(seed_docs, seed_text, "validateSeed"):
            missing.append(f"gps_uart_mode {value}")
        gps_function = function_source(driver_source, "validateGpsValue")
        if gps_function is not None:
            for value in gps_missing(seed_docs, driver_source, "validateGpsValue", False):
                missing.append(f"GPS value {value}")
    except ValueError as error:
        missing.append(str(error))

for namespace, key in re.findall(r"`(ui|control|camera|setting)\.([a-z0-9_]+)`", docs):
    # Production query code switches on the suffix. Control/camera/setting
    # names are likewise registered without the scenario namespace prefix.
    if f'"{key}"' not in sources and f'"{namespace}.{key}"' not in sources:
        missing.append(f"{namespace}.{key}")

if check_links:
    for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", docs):
        if target.startswith(("http://", "https://", "#")):
            continue
        target_path = (docs_path.parent / target).resolve()
        if not target_path.is_file():
            missing.append(f"link {target}")

if missing:
    print("missing documented simulator tokens:", file=sys.stderr)
    for item in missing:
        print(f"  {item}", file=sys.stderr)
    raise SystemExit(1)

print("sim documentation tokens are present")
if check_links:
    print("sim documentation links resolve")
PY
