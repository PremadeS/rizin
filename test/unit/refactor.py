import os, re

def refactor_file(filepath):
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    orig = content

    # 1. Standalone `rz_cons_new();` -> `RzCons *cons = rz_cons_new();`
    matches = list(re.finditer(r"\brz_cons_new\s*\(\s*\)\s*;", content))
    for m in reversed(matches):
        start_pos = m.start()
        line_start = content.rfind("\n", 0, start_pos)
        line_start = 0 if line_start == -1 else line_start + 1
        prefix = content[line_start:start_pos].strip()

        # Skip if already assigned or part of struct
        if "RzCons" in prefix or "->" in prefix or "." in prefix or "=" in prefix:
            continue

        indent = content[line_start:start_pos][:len(content[line_start:start_pos]) - len(content[line_start:start_pos].lstrip())]
        new_stmt = f"{indent}RzCons *cons = rz_cons_new();"
        content = content[:line_start] + new_stmt + content[m.end():]

    # 2. Untyped `var = rz_cons_new();` -> `RzCons *var = rz_cons_new();`
    matches = list(re.finditer(r"\b([a-zA-Z_]\w*)\s*=\s*rz_cons_new\s*\(\s*\)\s*;", content))
    for m in reversed(matches):
        var_name = m.group(1)
        start_pos = m.start()
        line_start = content.rfind("\n", 0, start_pos)
        line_start = 0 if line_start == -1 else line_start + 1
        prefix = content[line_start:start_pos].strip()

        if "RzCons" in prefix or "struct" in prefix or "->" in prefix or "." in prefix:
            continue

        indent = content[line_start:start_pos][:len(content[line_start:start_pos]) - len(content[line_start:start_pos].lstrip())]
        new_stmt = f"{indent}RzCons *{var_name} = rz_cons_new();"
        content = content[:line_start] + new_stmt + content[m.end():]

    SKIP_FUNCS = {"new", "new0", "is_initialized"}

    # 3. Inject `cons` into all rz_cons_* calls (including rz_cons_reset, rz_cons_free, etc.)
    matches = list(re.finditer(r"\brz_cons_([a-zA-Z0-9_]+)\s*\(", content))
    for m in reversed(matches):
        func_name = m.group(1)
        if func_name in SKIP_FUNCS:
            continue

        # Skip if this function call is part of an assignment like `RzCons *cons = rz_cons_new()`
        prefix_chunk = content[max(0, m.start() - 35):m.start()]
        if re.search(r"RzCons\s*\*\s*\w*\s*=\s*$", prefix_chunk):
            continue

        start_paren = m.end() - 1
        depth = 1
        i = start_paren + 1
        in_string = False
        str_char = ""

        # Parse balanced parentheses
        while i < len(content) and depth > 0:
            ch = content[i]
            if in_string:
                if ch == "\\" and i + 1 < len(content):
                    i += 2
                    continue
                if ch == str_char:
                    in_string = False
            else:
                if ch in ('"', "'"):
                    in_string = True
                    str_char = ch
                elif ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
            if depth == 0:
                break
            i += 1

        if depth == 0:
            end_paren = i
            args_text = content[start_paren + 1:end_paren]

            # Extract first argument
            first_arg = ""
            arg_depth = 0
            in_str = False
            for c in args_text:
                if in_str:
                    if c == str_char: in_str = False
                    first_arg += c
                else:
                    if c in ('"', "'"):
                        in_str = True
                        str_char = c
                        first_arg += c
                    elif c in ('(', '{', '['):
                        arg_depth += 1
                        first_arg += c
                    elif c in (')', '}', ']'):
                        arg_depth -= 1
                        first_arg += c
                    elif c == ',' and arg_depth == 0:
                        break
                    else:
                        first_arg += c

            first_arg_clean = first_arg.strip()

            is_cons = (
                first_arg_clean in ("c", "cons") or
                bool(re.search(r"(\b|->|\.)cons\b", first_arg_clean)) or
                bool(re.search(r"(\b|->|\.)c\b", first_arg_clean)) or
                "RzCons" in first_arg_clean
            )

            # Look backward for variable name in scope (cons, c, core->cons, ctx->cons)
            pos = m.start()
            scope_chunk = content[max(0, pos - 2500):pos]

            cons_var = "cons"
            var_match = re.findall(r"RzCons\s*\*\s*([a-zA-Z_]\w*)", scope_chunk)
            if var_match:
                cons_var = var_match[-1]
            elif "core->cons" in scope_chunk:
                cons_var = "core->cons"
            elif "ctx->cons" in scope_chunk:
                cons_var = "ctx->cons"

            # Refactor rz_cons_break_push / rz_cons_break_pop -> rz_interrupt_break_*
            if func_name == "break_push":
                full_call_start = m.start()
                if not is_cons:
                    new_args = f"{cons_var}->intr" if not args_text.strip() else f"{cons_var}->intr, " + args_text
                else:
                    new_args = f"{first_arg_clean}->intr" + args_text[len(first_arg):]
                content = content[:full_call_start] + f"rz_interrupt_break_push({new_args})" + content[end_paren + 1:]
                continue
            elif func_name == "break_pop":
                full_call_start = m.start()
                if not is_cons:
                    new_args = f"{cons_var}->intr"
                else:
                    new_args = f"{first_arg_clean}->intr"
                content = content[:full_call_start] + f"rz_interrupt_break_pop({new_args})" + content[end_paren + 1:]
                continue

            # Inject cons handle as first argument if missing
            if not is_cons:
                new_args = cons_var if not args_text.strip() else f"{cons_var}, " + args_text
                content = content[:start_paren + 1] + new_args + content[end_paren:]

    if content != orig:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)

for root, dirs, files in os.walk("."):
    for file in files:
        if file.endswith((".c", ".h")):
            refactor_file(os.path.join(root, file))
