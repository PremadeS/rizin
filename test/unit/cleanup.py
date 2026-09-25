import os, re

# Map of functions that DO NOT take RzCons* as their first argument, mapped to their expected parameter count
NO_CONS_FUNCS = {
    "rz_cons_canvas_new": 2,
    "rz_cons_canvas_free": 1,
    "rz_cons_canvas_clear": 1,
    "rz_cons_canvas_to_string": 1,
    "rz_cons_canvas_write": 2,
    "rz_cons_canvas_gotoxy": 3,
    "rz_cons_canvas_box": 6,
    "rz_cons_canvas_resize": 3,
    "rz_cons_canvas_fill": 6,
    "rz_cons_new": 0,
    "rz_cons_default_context_is_interactive": 0,
    "rz_cons_pipe_open": 3,
    "rz_cons_pipe_close": 1,
    "rz_cons_context_new": 1,
    "rz_cons_context_free": 1,
    "rz_cons_context_break_push": 3,
    "rz_cons_context_break_pop": 1,
    "rz_cons_context_break": 1,
    "rz_cons_get_cur_line": 0,
    "rz_cons_swap_ground": 1,
    "rz_cons_pal_free": 1,
    "rz_cons_pal_init": 1,
    "rz_cons_pal_copy": 2,
    "rz_cons_pal_len": 0,
    "rz_cons_rgb_parse": 5,
    "rz_cons_rgb_tostring": 3,
    "rz_cons_isatty": 0,
    "rz_cons_rainbow_free": 1,
    "rz_cons_rainbow_new": 2,
    "rz_cons_grep_strip": 2,
    "rz_cons_rgb_init": 0,
    "rz_cons_rgb_str_mode": 4,
    "rz_cons_bind": 1,
    "rz_cons_get_rune": 1,
    "rz_histogram_horizontal": 4,
    "rz_histogram_vertical": 4,
    "rz_histogram_interactive_horizontal": 2,
    "rz_histogram_options_new": 0,
    "rz_histogram_options_free": 1,
    "rz_histogram_interactive_new": 2,
    "rz_histogram_interactive_free": 1,
    "rz_histogram_interactive_zoom_in": 1,
    "rz_histogram_interactive_zoom_out": 1,
}

def refactor_file(filepath):
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    orig = content

    # 1. Replace rz_cons_is_breaked(cons) -> rz_interrupt_is_breaked(cons->intr)
    matches = list(re.finditer(r"\brz_cons_is_breaked\s*\(", content))
    for m in reversed(matches):
        start_paren = m.end() - 1
        depth = 1
        i = start_paren + 1
        in_string = False
        str_char = ""

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
                elif ch == '(': depth += 1
                elif ch == ')': depth -= 1
            if depth == 0: break
            i += 1

        if depth == 0:
            end_paren = i
            args_text = content[start_paren + 1:end_paren].strip()
            arg = args_text if args_text else "cons"

            if arg.endswith("->intr") or arg.endswith(".intr"):
                intr_arg = arg
            else:
                intr_arg = f"{arg}->intr"

            replacement = f"rz_interrupt_is_breaked({intr_arg})"
            content = content[:m.start()] + replacement + content[end_paren + 1:]

    # 2. Strip mistakenly injected first arguments for NO_CONS_FUNCS
    pattern = r"\b(" + "|".join(re.escape(k) for k in NO_CONS_FUNCS.keys()) + r")\s*\("
    matches = list(re.finditer(pattern, content))

    for m in reversed(matches):
        func_name = m.group(1)
        expected_args = NO_CONS_FUNCS[func_name]
        start_paren = m.end() - 1
        depth = 1
        i = start_paren + 1
        in_string = False
        str_char = ""

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
                elif ch == '(': depth += 1
                elif ch == ')': depth -= 1
            if depth == 0: break
            i += 1

        if depth == 0:
            end_paren = i
            args_text = content[start_paren + 1:end_paren]

            # Split arguments by top-level comma
            args = []
            curr = ""
            arg_depth = 0
            in_str = False
            for c in args_text:
                if in_str:
                    if c == str_char: in_str = False
                    curr += c
                else:
                    if c in ('"', "'"):
                        in_str = True
                        str_char = c
                        curr += c
                    elif c in ('(', '{', '['): arg_depth += 1; curr += c
                    elif c in (')', '}', ']'): arg_depth -= 1; curr += c
                    elif c == ',' and arg_depth == 0:
                        args.append(curr)
                        curr = ""
                    else:
                        curr += c
            if curr.strip() or args:
                args.append(curr)

            clean_args = [a for a in args if a.strip()]

            # If arguments count exceeds expected parameters, remove the extra first argument
            if len(clean_args) > expected_args:
                remaining_args = clean_args[1:]
                new_args_text = ", ".join(remaining_args)
                content = content[:start_paren + 1] + new_args_text + content[end_paren:]

    if content != orig:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)

for root, dirs, files in os.walk("."):
    for file in files:
        if file.endswith((".c", ".h")):
            refactor_file(os.path.join(root, file))
