python3 -c '
import os, re

def refactor(filepath):
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    
    orig = content
    
    # 1. Add `RzCons *` to `var = rz_cons_new()` if no type declaration precedes it
    content = re.sub(
        r"(?m)^(\s*)([a-zA-Z_]\w*)\s*=\s*rz_cons_new\s*\(\s*\)",
        r"\1RzCons *\2 = rz_cons_new()",
        content
    )

    # 2. Add `cons` as the first argument for `rz_cons_*` calls if missing
    matches = list(re.finditer(r"\brz_cons_([a-zA-Z0-9_]+)\s*\(", content))
    for m in reversed(matches):
        func_name = m.group(1)
        # Skip constructors that create new instances
        if func_name in ("new", "new0"):
            continue

        start_paren = m.end() - 1
        depth, i = 1, start_paren + 1
        in_string, str_char = False, ""

        # Parse balanced closing parenthesis
        while i < len(content) and depth > 0:
            ch = content[i]
            if in_string:
                if ch == "\\" and i + 1 < len(content):
                    i += 2
                    continue
                if ch == str_char:
                    in_string = False
            else:
                if ch == "\"" or ch == "'":
                    in_string = True
                    str_char = ch
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
            if depth == 0:
                break
            i += 1

        if depth == 0:
            end_paren = i
            args_text = content[start_paren + 1:end_paren]

            # Extract the first argument
            first_arg, arg_depth, in_str = "", 0, False
            for c in args_text:
                if in_str:
                    if c == str_char: in_str = False
                    first_arg += c
                else:
                    if c == "\"" or c == "'":
                        in_str = True
                        str_char = c
                        first_arg += c
                    elif c in ("(", "{", "["):
                        arg_depth += 1
                        first_arg += c
                    elif c in (")", "}", "]"):
                        arg_depth -= 1
                        first_arg += c
                    elif c == "," and arg_depth == 0:
                        break
                    else:
                        first_arg += c

            first_arg_clean = first_arg.strip()

            # Check if first argument is already c, cons, member access (->cons), or a type definition (RzCons *)
            is_cons = (
                first_arg_clean in ("c", "cons") or
                bool(re.search(r"(\b|->|\.)cons\b", first_arg_clean)) or
                bool(re.match(r"^(\*|&)?(c|cons)\b", first_arg_clean)) or
                "RzCons" in first_arg_clean
            )

            if not is_cons:
                new_args = "cons" if not args_text.strip() else "cons, " + args_text
                content = content[:start_paren + 1] + new_args + content[end_paren:]

    if content != orig:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)

for root, dirs, files in os.walk("."):
    for file in files:
        if file.endswith((".c", ".h")):
            refactor(os.path.join(root, file))
'
