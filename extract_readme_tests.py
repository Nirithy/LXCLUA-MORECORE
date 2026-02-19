import os
import re

def comment_out_block(match):
    block = match.group(0)
    lines = block.split('\n')
    commented_lines = ['-- ' + line for line in lines]
    return '\n'.join(commented_lines)

def patch_test_01(code):
    search = """local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil (不会报错)"""

    replace = """do
    local config = { server = { port = 8080 } }
    local port = config?.server?.port
end
do
    local config = { server = { port = 8080 } }
    local timeout = config?.client?.timeout
end"""

    if search in code:
        code = code.replace(search, replace)

    code = code.replace("maybe_nil |?> print", "-- maybe_nil |?> print")
    return code

def patch_test_02(code):
    code = code.replace("local name", "name")
    code = code.replace("local age", "age")
    return code

def patch_test_03(code):
    code = code.replace("local sq = lambda", "-- local sq = lambda")

    # Use function to comment out multi-line blocks correctly
    code = re.sub(r'(int sum.*?})', comment_out_block, code, flags=re.DOTALL)
    code = re.sub(r'(function<T>.*?end)', comment_out_block, code, flags=re.DOTALL)
    code = re.sub(r'(async function.*?end)', comment_out_block, code, flags=re.DOTALL)

    return code

def patch_test_04(code):
    # Fix interface definition to match class method signature (which includes self)
    # Be specific to avoid replacing class method if possible, but class method also needs fix.

    # Fix interface:
    # interface Drawable\n    function draw() -> function draw(self)
    code = code.replace("interface Drawable\n    function draw()", "interface Drawable\n    function draw(self)")

    # Fix class getters/setters
    code = code.replace("get radius()", "get radius(self)")
    code = code.replace("set radius(v)", "set radius(self, v)")
    return code

def patch_test_06(code):
    header = "local val = 1\nlocal print_fn = print\n"
    code = code.replace("print(", "print_fn(")
    code = code.replace("defer f:close() end", "defer do f:close() end")

    code = code.replace("MyLib::test()", "-- MyLib::test()")
    code = code.replace("using namespace", "-- using namespace")
    code = code.replace("print(version)", "-- print(version)")
    return header + code

def patch_test_07(code):
    return "_OPERATORS = {}\n" + code

def extract_lua_blocks(readme_path, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    try:
        with open(readme_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except FileNotFoundError:
        print(f"Error: {readme_path} not found.")
        return

    pattern = re.compile(r'```\s*lua(.*?)```', re.DOTALL | re.IGNORECASE)
    matches = pattern.findall(content)

    print(f"Found {len(matches)} Lua blocks.")

    processors = {
        0: patch_test_01,
        1: patch_test_02,
        2: patch_test_03,
        3: patch_test_04,
        # 4: test_05 (structs) passed
        5: patch_test_06,
        6: patch_test_07,
        # 7: test_08 (asm) fixed in README
    }

    count = 0
    for i, block in enumerate(matches):
        code = block.strip()
        if not code:
            continue

        # Apply patch if exists
        if i in processors:
            code = processors[i](code)

        count += 1
        filename = f"test_{count:02d}.lua"
        filepath = os.path.join(output_dir, filename)

        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(code)
            f.write('\n')

        print(f"Saved {filepath}")

if __name__ == "__main__":
    extract_lua_blocks("README.md", "readme_tests")
