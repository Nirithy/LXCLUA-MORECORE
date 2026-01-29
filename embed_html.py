#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将lxclua.js内嵌到HTML中生成单文件HTML

功能描述：读取lxclua.js和lxclua.html模板，将JS内嵌生成单文件lxclua_standalone.html
参数说明：无
返回值说明：生成lxclua_standalone.html文件
"""

import os

def embed_js_to_html():
    """
    将JS文件内嵌到HTML中生成单文件
    
    功能描述：读取JS文件内容，替换HTML中的外部引用为内联脚本
    参数说明：无
    返回值说明：无，直接写入文件
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    js_path = os.path.join(script_dir, 'lxclua.js')
    output_path = os.path.join(script_dir, 'lxclua_standalone.html')
    
    # 读取JS文件
    with open(js_path, 'r', encoding='utf-8') as f:
        js_content = f.read()
    
    # 生成内嵌HTML
    html_content = f'''<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LXCLUA WebAssembly</title>
    <style>
        body {{ font-family: monospace; padding: 10px; background: #1e1e1e; color: #d4d4d4; }}
        #output {{ background: #0d0d0d; padding: 10px; border-radius: 4px; white-space: pre-wrap; min-height: 200px; overflow: auto; }}
        #input {{ width: 100%; padding: 8px; margin-top: 10px; background: #252526; color: #d4d4d4; border: 1px solid #3c3c3c; box-sizing: border-box; }}
        button {{ padding: 8px 16px; margin-top: 10px; background: #0e639c; color: white; border: none; cursor: pointer; }}
        button:active {{ background: #094771; }}
    </style>
</head>
<body>
    <h2>LXCLUA WebAssembly</h2>
    <div id="output">Loading...</div>
    <textarea id="input" rows="5" placeholder="print('Hello LXCLUA!')">print(_VERSION)
for i=1,5 do print(i) end</textarea>
    <br>
    <button onclick="runLua()">Run</button>

    <script>
{js_content}
    </script>
    <script>
        var lua = null;
        var outputEl = document.getElementById('output');
        
        var Module = {{
            print: function(text) {{ outputEl.textContent += text + '\\n'; }},
            printErr: function(text) {{ outputEl.textContent += '[ERR] ' + text + '\\n'; }}
        }};
        
        LuaModule(Module).then(function(m) {{
            lua = m;
            outputEl.textContent = 'LXCLUA Ready!\\n';
            m.callMain(['-v']);
        }});
        
        function runLua() {{
            if (!lua) return;
            outputEl.textContent = '';
            var code = document.getElementById('input').value;
            lua.FS.writeFile('/tmp.lua', code);
            lua.callMain(['/tmp.lua']);
        }}
    </script>
</body>
</html>'''
    
    # 写入输出文件
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html_content)
    
    file_size = os.path.getsize(output_path) / 1024
    print(f'生成完成: {output_path}')
    print(f'文件大小: {file_size:.1f} KB')

if __name__ == '__main__':
    embed_js_to_html()
