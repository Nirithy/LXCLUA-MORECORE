#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将lxclua.js内嵌到HTML中生成单文件HTML

功能描述：读取lxclua.js，将JS内嵌生成单文件lxclua_standalone.html
参数说明：无
返回值说明：生成lxclua_standalone.html文件
"""

import os
import re

def minify_css(css):
    """
    压缩CSS代码
    
    功能描述：移除CSS中的注释、多余空白和换行
    参数说明：css - CSS字符串
    返回值说明：压缩后的CSS字符串
    """
    css = re.sub(r'/\*[\s\S]*?\*/', '', css)
    css = re.sub(r'\s+', ' ', css)
    css = re.sub(r'\s*([{};:,>])\s*', r'\1', css)
    css = re.sub(r';\s*}', '}', css)
    return css.strip()

def minify_js(js):
    """
    压缩JS代码
    
    功能描述：移除JS中的注释、多余空白和换行
    参数说明：js - JS字符串
    返回值说明：压缩后的JS字符串
    """
    js = re.sub(r'/\*[\s\S]*?\*/', '', js)
    js = re.sub(r'//[^\n]*', '', js)
    js = re.sub(r'\n\s*\n', '\n', js)
    js = re.sub(r'[ \t]+', ' ', js)
    js = re.sub(r'\s*([{};:,=+\-*/<>!&|?])\s*', r'\1', js)
    js = re.sub(r'\n+', '', js)
    return js.strip()

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
    
    with open(js_path, 'r', encoding='utf-8') as f:
        js_content = f.read()
    
    # CSS样式
    css_code = '''
body {
    font-family: monospace;
    padding: 10px;
    background: #1e1e1e;
    color: #d4d4d4;
}
.header {
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.header h2 { margin: 0; }
#langSelect {
    padding: 4px 8px;
    background: #4b5563;
    color: white;
    border: none;
    border-radius: 4px;
    font-family: inherit;
    cursor: pointer;
}
#output {
    background: #0d0d0d;
    padding: 10px;
    border-radius: 4px;
    white-space: pre-wrap;
    min-height: 200px;
    max-height: 300px;
    overflow: auto;
    transition: all 0.3s ease;
}
#output.expanded {
    min-height: 400px;
    max-height: 600px;
}
#editor-container {
    display: flex;
    margin-top: 10px;
    border: 1px solid #3c3c3c;
    border-radius: 4px;
    background: #252526;
    position: relative;
}
#editor-size {
    position: absolute;
    bottom: 4px;
    right: 8px;
    color: #858585;
    font-size: 11px;
    pointer-events: none;
    background: rgba(37,37,38,0.8);
    padding: 2px 4px;
    border-radius: 2px;
}
#lineNumbers {
    background: #1e1e1e;
    color: #858585;
    padding: 8px 8px 8px 4px;
    text-align: right;
    user-select: none;
    font-family: inherit;
    font-size: 14px;
    line-height: 1.5;
    border-right: 1px solid #3c3c3c;
    min-width: 30px;
    overflow: hidden;
    white-space: pre;
}
#input {
    flex: 1;
    padding: 8px;
    background: #252526;
    color: #d4d4d4;
    border: none;
    resize: vertical;
    font-family: inherit;
    font-size: 14px;
    line-height: 1.5;
    min-height: 150px;
    outline: none;
}
button {
    padding: 8px 16px;
    margin-top: 10px;
    background: #0e639c;
    color: white;
    border: none;
    cursor: pointer;
    margin-right: 5px;
    border-radius: 4px;
    font-family: inherit;
}
button:active {
    background: #094771;
}
#fileList {
    margin-top: 10px;
    background: #252526;
    padding: 10px;
    border-radius: 4px;
}
.file-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 4px 8px;
    margin: 2px 0;
    background: #1e1e1e;
    border-radius: 2px;
    flex-wrap: wrap;
}
.file-item span {
    color: #9cdcfe;
    flex: 1;
    min-width: 100px;
    cursor: pointer;
}
.file-item .file-size {
    color: #858585;
    font-size: 11px;
    margin-right: 8px;
}
.file-item .dir {
    color: #dcdcaa;
}
.file-item button {
    padding: 2px 8px;
    margin: 0 2px;
    font-size: 12px;
}
.file-item .del { background: #c53030; }
.file-item .mov { background: #6b7280; }
.file-item .ren { background: #d97706; }
.file-item .dl { background: #059669; }
.file-item .edit { background: #7c3aed; }
#fileInput, #importInput { display: none; }
.toolbar { margin-top: 10px; }
.toolbar button { background: #374151; }
#saveBtn { background: #10b981; display: none; }
#cancelEditBtn { background: #ef4444; display: none; }
#clearBtn { background: #6b7280; }
#copyBtn { background: #6b7280; }
#stopBtn { background: #dc2626; display: none; }
#runBtn.running { background: #f59e0b; }
.font-controls { display: inline; margin-left: 10px; }
.font-controls button { padding: 4px 10px; font-size: 14px; background: #4b5563; }
.font-controls select { padding: 4px 8px; background: #4b5563; color: white; border: none; font-size: 12px; cursor: pointer; border-radius: 4px; font-family: inherit; }
.terminal-controls { display: inline; margin-left: 5px; }
.terminal-controls button { padding: 4px 8px; font-size: 14px; background: #4b5563; margin: 0 2px; }
#fontFileInput { display: none; }
.drop-zone { border: 2px dashed #3c3c3c; border-radius: 4px; transition: all 0.3s; }
.drop-zone.drag-over { border-color: #0e639c; background: rgba(14,99,156,0.2); }
'''
    
    # 应用JS代码
    app_js = '''
var lua = null;
var outputEl = document.getElementById('output');
var inputEl = document.getElementById('input');
var lineNumEl = document.getElementById('lineNumbers');
var editorSizeEl = document.getElementById('editor-size');
var runBtn = document.getElementById('runBtn');
var fileCache = {};
var savedCode = '';
var isEditingFile = false;
var currentEditFile = null;
var isRunning = false;
var fontSize = 14;
var fontFamily = 'monospace';
var currentLang = 'en';
var isTerminalExpanded = false;

var i18n = {
    en: {
        title: 'LXCLUA WebAssembly',
        run: 'Run', stop: 'Stop', clear: 'Clear', copy: 'Copy',
        upload: 'Upload', newFile: 'New File', newDir: 'New Dir',
        save: 'Save', cancel: 'Cancel', export: 'Export', import: 'Import',
        font: 'Font', files: 'Files:', loading: 'Loading...',
        placeholder: "print('Hello LXCLUA!')",
        running: 'Running...', ready: 'LXCLUA Ready!',
        runStart: '--- Run ---', doneIn: '--- Done in ',
        draftSaved: '[INFO] Draft saved', outputCopied: '[INFO] Output copied!',
        copyFailed: '[ERR] Copy failed', fontLoaded: '[INFO] Font loaded: ',
        uploaded: '[INFO] Uploaded: ', fileTooLarge: '[WARN] File too large (>5MB): ',
        notPersist: ', will not persist', cacheWarn: '[WARN] Cache too large, some files not saved',
        importOk: 'Workspace imported!', importFail: '[ERR] Import failed: ',
        delConfirm: 'Delete ', newFileName: 'File name:', dirName: 'Directory name:',
        newName: 'New name:', edit: 'Edit', dl: 'DL', ren: 'Ren', del: 'Del',
        expandTerminal: 'Expand Terminal', collapseTerminal: 'Collapse Terminal'
    },
    zh: {
        title: 'LXCLUA 网页版',
        run: '运行', stop: '停止', clear: '清除', copy: '复制',
        upload: '上传', newFile: '新建文件', newDir: '新建目录',
        save: '保存', cancel: '取消', export: '导出', import: '导入',
        font: '字体', files: '文件列表:', loading: '加载中...',
        placeholder: "print('你好 LXCLUA!')",
        running: '运行中...', ready: 'LXCLUA 就绪!',
        runStart: '--- 运行 ---', doneIn: '--- 完成于 ',
        draftSaved: '[提示] 草稿已保存', outputCopied: '[提示] 已复制输出!',
        copyFailed: '[错误] 复制失败', fontLoaded: '[提示] 字体已加载: ',
        uploaded: '[提示] 已上传: ', fileTooLarge: '[警告] 文件过大 (>5MB): ',
        notPersist: ', 不会持久保存', cacheWarn: '[警告] 缓存过大，部分文件未保存',
        importOk: '工作区已导入!', importFail: '[错误] 导入失败: ',
        delConfirm: '删除 ', newFileName: '文件名:', dirName: '目录名:',
        newName: '新名称:', edit: '编辑', dl: '下载', ren: '重命名', del: '删除',
        expandTerminal: '展开终端', collapseTerminal: '收起终端'
    }
};

/**
 * 获取翻译文本
 */
function t(key) {
    return i18n[currentLang][key] || i18n['en'][key] || key;
}

/**
 * 切换语言
 */
function changeLang(lang) {
    currentLang = lang;
    try { localStorage.setItem('lxclua_lang', lang); } catch(e) {}
    applyLang();
}

/**
 * 应用语言到界面
 */
function applyLang() {
    document.getElementById('pageTitle').textContent = t('title');
    document.getElementById('runBtn').textContent = isRunning ? t('running') : t('run');
    document.getElementById('stopBtn').textContent = t('stop');
    document.getElementById('clearBtn').textContent = t('clear');
    document.getElementById('copyBtn').textContent = t('copy');
    document.getElementById('uploadBtn').textContent = t('upload');
    document.getElementById('newFileBtn').textContent = t('newFile');
    document.getElementById('newDirBtn').textContent = t('newDir');
    document.getElementById('saveBtn').textContent = t('save');
    document.getElementById('cancelEditBtn').textContent = t('cancel');
    document.getElementById('exportBtn').textContent = t('export');
    document.getElementById('importBtn').textContent = t('import');
    document.getElementById('fontBtn').textContent = t('font');
    document.getElementById('terminalBtn').textContent = isTerminalExpanded ? t('collapseTerminal') : t('expandTerminal');
    updateFileList();
}

/**
 * 切换终端大小
 * 功能描述：展开或收起终端显示区域
 */
function toggleTerminalSize() {
    isTerminalExpanded = !isTerminalExpanded;
    var terminalBtn = document.getElementById('terminalBtn');
    var outputEl = document.getElementById('output');
    
    if (isTerminalExpanded) {
        outputEl.classList.add('expanded');
        outputEl.style.minHeight = '400px';
        outputEl.style.maxHeight = '600px';
        terminalBtn.textContent = t('collapseTerminal');
        try { localStorage.setItem('lxclua_terminal_height', 400); } catch(e) {}
    } else {
        outputEl.classList.remove('expanded');
        outputEl.style.minHeight = '200px';
        outputEl.style.maxHeight = '300px';
        terminalBtn.textContent = t('expandTerminal');
        try { localStorage.setItem('lxclua_terminal_height', 200); } catch(e) {}
    }
    
    try { localStorage.setItem('lxclua_terminal_expanded', isTerminalExpanded); } catch(e) {}
}

/**
 * 加载终端大小设置
 * 功能描述：从localStorage恢复终端展开状态
 */
function loadTerminalSize() {
    try {
        var saved = localStorage.getItem('lxclua_terminal_expanded');
        if (saved !== null) {
            isTerminalExpanded = saved === 'true';
            var outputEl = document.getElementById('output');
            if (isTerminalExpanded) {
                outputEl.classList.add('expanded');
            }
        }
    } catch(e) {}
}

/**
 * 加载语言设置
 */
function loadLang() {
    try {
        var saved = localStorage.getItem('lxclua_lang');
        if (saved) {
            currentLang = saved;
        } else {
            var sysLang = navigator.language || navigator.userLanguage || 'en';
            currentLang = sysLang.startsWith('zh') ? 'zh' : 'en';
        }
        document.getElementById('langSelect').value = currentLang;
    } catch(e) {}
}

/**
 * 更新编辑器大小显示
 * 功能描述：实时显示编辑内容的字节大小
 */
function updateEditorSize() {
    var bytes = new Blob([inputEl.value]).size;
    editorSizeEl.textContent = formatSize(bytes);
}

/**
 * 切换字体
 * 功能描述：更改整个页面的字体
 */
function changeFont(font) {
    fontFamily = font;
    document.body.style.fontFamily = font;
    try { localStorage.setItem('lxclua_font', font); } catch(e) {}
}

/**
 * 上传自定义字体
 * 功能描述：加载用户上传的字体文件并应用，小于2MB的字体会保存
 */
function uploadFont(file) {
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function(e) {
        var fontName = 'CustomFont-' + Date.now();
        var fontData = e.target.result;
        var style = document.createElement('style');
        style.textContent = '@font-face { font-family: "' + fontName + '"; src: url(' + fontData + '); }';
        document.head.appendChild(style);
        var select = document.getElementById('fontSelect');
        var option = document.createElement('option');
        option.value = '"' + fontName + '", monospace';
        option.textContent = file.name.replace(/\\.[^.]+$/, '');
        select.appendChild(option);
        select.value = option.value;
        changeFont(option.value);
        if (file.size < 2000000) {
            try {
                localStorage.setItem('lxclua_customfont', JSON.stringify({
                    name: fontName,
                    data: fontData,
                    displayName: option.textContent
                }));
                outputEl.textContent += t('fontLoaded') + file.name + ' (saved)\\n';
            } catch(ex) {
                outputEl.textContent += t('fontLoaded') + file.name + ' (not saved)\\n';
            }
        } else {
            outputEl.textContent += t('fontLoaded') + file.name + ' (too large to save)\\n';
        }
        outputEl.scrollTop = outputEl.scrollHeight;
    };
    reader.readAsDataURL(file);
    document.getElementById('fontFileInput').value = '';
}

/**
 * 加载字体设置
 */
function loadFont() {
    try {
        var customFont = localStorage.getItem('lxclua_customfont');
        if (customFont) {
            var font = JSON.parse(customFont);
            var style = document.createElement('style');
            style.textContent = '@font-face { font-family: "' + font.name + '"; src: url(' + font.data + '); }';
            document.head.appendChild(style);
            var select = document.getElementById('fontSelect');
            var option = document.createElement('option');
            option.value = '"' + font.name + '", monospace';
            option.textContent = font.displayName;
            select.appendChild(option);
        }
        var saved = localStorage.getItem('lxclua_font');
        if (saved) {
            fontFamily = saved;
            document.body.style.fontFamily = saved;
            document.getElementById('fontSelect').value = saved;
        }
    } catch(e) {}
}

/**
 * 更新行号显示
 * 功能描述：根据输入框内容更新左侧行号
 */
function updateLineNumbers() {
    var lines = inputEl.value.split('\\n');
    var nums = '';
    for (var i = 1; i <= lines.length; i++) {
        nums += i + '\\n';
    }
    lineNumEl.textContent = nums;
}

/**
 * 同步滚动
 * 功能描述：让行号与编辑区同步滚动
 */
function syncScroll() {
    lineNumEl.scrollTop = inputEl.scrollTop;
}

/**
 * 处理键盘事件
 * 功能描述：Tab缩进、Ctrl+Enter运行、Ctrl+S保存
 */
function handleKeydown(e) {
    if (e.key === 'Tab') {
        e.preventDefault();
        var start = inputEl.selectionStart;
        var end = inputEl.selectionEnd;
        inputEl.value = inputEl.value.substring(0, start) + '    ' + inputEl.value.substring(end);
        inputEl.selectionStart = inputEl.selectionEnd = start + 4;
        updateLineNumbers();
        saveDraft();
    }
    if (e.ctrlKey && e.key === 'Enter') {
        e.preventDefault();
        runLua();
    }
    if (e.ctrlKey && e.key === 's') {
        e.preventDefault();
        if (isEditingFile) {
            saveAndExit();
        } else {
            saveDraft();
            outputEl.textContent += t('draftSaved') + '\\n';
        }
    }
}

/**
 * 调整字体大小
 * 功能描述：增加或减少编辑器和终端字体
 */
function changeFontSize(delta) {
    fontSize = Math.max(10, Math.min(24, fontSize + delta));
    inputEl.style.fontSize = fontSize + 'px';
    lineNumEl.style.fontSize = fontSize + 'px';
    outputEl.style.fontSize = fontSize + 'px';
    try { localStorage.setItem('lxclua_fontsize', fontSize); } catch(e) {}
}

/**
 * 调整终端大小
 * 功能描述：增加或减少终端高度
 */
function changeTerminalSize(delta) {
    var outputEl = document.getElementById('output');
    var currentHeight = parseInt(window.getComputedStyle(outputEl).minHeight);
    var newHeight = Math.max(100, Math.min(800, currentHeight + delta));
    
    outputEl.style.minHeight = newHeight + 'px';
    outputEl.style.maxHeight = (newHeight + 100) + 'px';
    
    try { localStorage.setItem('lxclua_terminal_height', newHeight); } catch(e) {}
}

/**
 * 加载终端高度设置
 * 功能描述：从localStorage恢复终端高度
 */
function loadTerminalHeight() {
    try {
        var saved = localStorage.getItem('lxclua_terminal_height');
        if (saved) {
            var height = parseInt(saved);
            var outputEl = document.getElementById('output');
            outputEl.style.minHeight = height + 'px';
            outputEl.style.maxHeight = (height + 100) + 'px';
        }
    } catch(e) {}
}

/**
 * 加载字体大小
 */
function loadFontSize() {
    try {
        var saved = localStorage.getItem('lxclua_fontsize');
        if (saved) {
            fontSize = parseInt(saved);
            inputEl.style.fontSize = fontSize + 'px';
            lineNumEl.style.fontSize = fontSize + 'px';
        }
    } catch(e) {}
}

/**
 * 保存草稿
 * 功能描述：自动保存代码到localStorage
 */
function saveDraft() {
    if (!isEditingFile) {
        try {
            localStorage.setItem('lxclua_draft', inputEl.value);
        } catch (e) {}
    }
}

/**
 * 加载草稿
 * 功能描述：从localStorage恢复代码
 */
function loadDraft() {
    try {
        var draft = localStorage.getItem('lxclua_draft');
        if (draft) inputEl.value = draft;
        updateLineNumbers();
    } catch (e) {}
}

/**
 * 清除控制台
 * 功能描述：清空输出区域
 */
function clearConsole() {
    outputEl.textContent = '';
}

/**
 * 加载文件缓存
 * 功能描述：从localStorage加载文件缓存
 */
function loadCache() {
    try {
        var saved = localStorage.getItem('lxclua_files');
        if (saved) fileCache = JSON.parse(saved);
    } catch (e) {}
}

/**
 * 保存文件缓存
 * 功能描述：将文件缓存保存到localStorage（跳过大文件）
 */
function saveCache() {
    try {
        var saveData = {};
        var names = Object.keys(fileCache);
        for (var i = 0; i < names.length; i++) {
            var name = names[i];
            var content = fileCache[name];
            if (content === null || content.length < 500000) {
                saveData[name] = content;
            }
        }
        localStorage.setItem('lxclua_files', JSON.stringify(saveData));
    } catch (e) {
        outputEl.textContent += t('cacheWarn') + '\n';
        outputEl.scrollTop = outputEl.scrollHeight;
    }
}

/**
 * 恢复文件到FS
 * 功能描述：将缓存的文件恢复到Emscripten文件系统
 */
function restoreFilesToFS() {
    var names = Object.keys(fileCache);
    for (var i = 0; i < names.length; i++) {
        var name = names[i];
        if (fileCache[name] === null) {
            try { lua.FS.mkdir('/' + name); } catch (e) {}
        } else {
            var parts = name.split('/');
            if (parts.length > 1) {
                var dir = parts.slice(0, -1).join('/');
                try { lua.FS.mkdir('/' + dir); } catch (e) {}
            }
            lua.FS.writeFile('/' + name, fileCache[name]);
        }
    }
}

/**
 * 初始化拖放上传
 * 功能描述：支持拖放文件到文件列表区域上传
 */
function initDragDrop() {
    var dropZone = document.getElementById('fileList');
    dropZone.classList.add('drop-zone');
    
    dropZone.addEventListener('dragover', function(e) {
        e.preventDefault();
        dropZone.classList.add('drag-over');
    });
    
    dropZone.addEventListener('dragleave', function(e) {
        e.preventDefault();
        dropZone.classList.remove('drag-over');
    });
    
    dropZone.addEventListener('drop', function(e) {
        e.preventDefault();
        dropZone.classList.remove('drag-over');
        if (e.dataTransfer.files.length > 0) {
            uploadFiles(e.dataTransfer.files);
        }
    });
}

// 绑定事件
inputEl.addEventListener('input', function() {
    updateLineNumbers();
    updateEditorSize();
    saveDraft();
});
inputEl.addEventListener('scroll', syncScroll);
inputEl.addEventListener('keydown', handleKeydown);
document.addEventListener('keydown', function(e) {
    if (e.ctrlKey && e.key === 's') {
        e.preventDefault();
    }
});

// 初始化
loadCache();
loadDraft();
loadFontSize();
loadFont();
loadLang();
loadTerminalSize();
loadTerminalHeight();
updateEditorSize();
setTimeout(function() {
    initDragDrop();
    applyLang();
}, 100);

var Module = {
    print: function(text) {
        outputEl.textContent += text + '\\n';
        outputEl.scrollTop = outputEl.scrollHeight;
    },
    printErr: function(text) {
        outputEl.textContent += '[ERR] ' + text + '\\n';
        outputEl.scrollTop = outputEl.scrollHeight;
    }
};

LuaModule(Module).then(function(m) {
    lua = m;
    restoreFilesToFS();
    updateFileList();
    outputEl.textContent = 'LXCLUA Ready!\\n';
    m.callMain(['-v']);
});

/**
 * 运行Lua代码
 * 功能描述：执行编辑器中的Lua代码
 */
function runLua() {
    if (!lua || isRunning) return;
    isRunning = true;
    runBtn.textContent = 'Running...';
    runBtn.classList.add('running');
    document.getElementById('stopBtn').style.display = 'inline';
    outputEl.textContent += '\\n--- Run ---\\n';
    outputEl.scrollTop = outputEl.scrollHeight;
    var code = inputEl.value;
    lua.FS.writeFile('/tmp.lua', code);
    var startTime = performance.now();
    setTimeout(function() {
        try {
            lua.callMain(['/tmp.lua']);
        } catch (e) {
            outputEl.textContent += '[ERR] ' + e + '\\n';
        }
        var endTime = performance.now();
        var elapsed = ((endTime - startTime) / 1000).toFixed(3);
        outputEl.textContent += '--- Done in ' + elapsed + 's ---\\n';
        outputEl.scrollTop = outputEl.scrollHeight;
        syncFromFS();
        isRunning = false;
        runBtn.textContent = 'Run';
        runBtn.classList.remove('running');
        document.getElementById('stopBtn').style.display = 'none';
    }, 10);
}

/**
 * 终止运行
 * 功能描述：终止正在运行的Lua代码（通过重新加载页面）
 */
function stopLua() {
    if (isRunning) {
        outputEl.textContent += '\\n[WARN] WASM running synchronously, reloading...\\n';
        location.reload();
    }
}

/**
 * 判断是否为目录
 */
function isDirectory(mode) {
    return (mode & 61440) === 16384;
}

/**
 * 从FS同步文件
 */
function syncFromFS() {
    try {
        var files = lua.FS.readdir('/');
        for (var i = 0; i < files.length; i++) {
            var name = files[i];
            if (name === '.' || name === '..' || name === 'tmp' || name === 'home' || name === 'dev' || name === 'proc' || name === 'tmp.lua') continue;
            try {
                var stat = lua.FS.stat('/' + name);
                if (isDirectory(stat.mode)) {
                    if (!(name in fileCache)) fileCache[name] = null;
                    syncDirFromFS(name);
                } else {
                    var content = lua.FS.readFile('/' + name, { encoding: 'utf8' });
                    fileCache[name] = content;
                }
            } catch (e) {}
        }
        saveCache();
        updateFileList();
    } catch (e) {}
}

/**
 * 递归同步目录
 */
function syncDirFromFS(dir) {
    try {
        var files = lua.FS.readdir('/' + dir);
        for (var i = 0; i < files.length; i++) {
            var name = files[i];
            if (name === '.' || name === '..') continue;
            var fullPath = dir + '/' + name;
            try {
                var stat = lua.FS.stat('/' + fullPath);
                if (isDirectory(stat.mode)) {
                    if (!(fullPath in fileCache)) fileCache[fullPath] = null;
                    syncDirFromFS(fullPath);
                } else {
                    var content = lua.FS.readFile('/' + fullPath, { encoding: 'utf8' });
                    fileCache[fullPath] = content;
                }
            } catch (e) {}
        }
    } catch (e) {}
}

/**
 * 格式化文件大小
 * 功能描述：将字节数转换为可读格式
 */
function formatSize(bytes) {
    if (bytes < 1024) return bytes + 'B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + 'KB';
    return (bytes / 1024 / 1024).toFixed(1) + 'MB';
}

/**
 * 复制控制台输出
 * 功能描述：将控制台内容复制到剪贴板
 */
function copyOutput() {
    navigator.clipboard.writeText(outputEl.textContent).then(function() {
        outputEl.textContent += t('outputCopied') + '\\n';
        outputEl.scrollTop = outputEl.scrollHeight;
    }).catch(function() {
        outputEl.textContent += t('copyFailed') + '\\n';
    });
}

/**
 * 新建空文件
 * 功能描述：创建一个新的空Lua文件
 */
function createFile() {
    var name = prompt(t('newFileName'), 'new.lua');
    if (name) {
        fileCache[name] = '';
        if (lua) lua.FS.writeFile('/' + name, '');
        saveCache();
        updateFileList();
    }
}

/**
 * 更新文件列表
 */
function updateFileList() {
    var listEl = document.getElementById('fileList');
    var names = Object.keys(fileCache).sort();
    
    if (names.length === 0) {
        // 没有文件时隐藏文件列表
        listEl.style.display = 'none';
    } else {
        // 有文件时显示文件列表并生成内容
        listEl.style.display = 'block';
        var html = '<b>' + t('files') + '</b><br>';
        for (var i = 0; i < names.length; i++) {
            var name = names[i];
            var isDir = fileCache[name] === null;
            var size = isDir ? '' : formatSize(fileCache[name].length);
            html += '<div class="file-item">';
            html += '<span class="' + (isDir ? 'dir' : '') + '" ondblclick="' + (isDir ? '' : 'editFile(\\'' + name + '\\')') + '">' + name + (isDir ? '/' : '') + '</span>';
            if (!isDir) {
                html += '<span class="file-size">' + size + '</span>';
                html += '<button class="edit" onclick="editFile(\\'' + name + '\\')">' + t('edit') + '</button>';
                html += '<button class="dl" onclick="downloadFile(\\'' + name + '\\')">' + t('dl') + '</button>';
            }
            html += '<button class="ren" onclick="renameFile(\\'' + name + '\\')">' + t('ren') + '</button>';
            html += '<button class="del" onclick="deleteFile(\\'' + name + '\\')">' + t('del') + '</button></div>';
        }
        listEl.innerHTML = html;
    }
}

/**
 * 编辑文件
 */
function editFile(name) {
    savedCode = inputEl.value;
    inputEl.value = fileCache[name] || '';
    isEditingFile = true;
    currentEditFile = name;
    document.getElementById('saveBtn').style.display = 'inline';
    document.getElementById('cancelEditBtn').style.display = 'inline';
    updateLineNumbers();
}

/**
 * 保存并退出编辑
 */
function saveAndExit() {
    if (currentEditFile) {
        fileCache[currentEditFile] = inputEl.value;
        if (lua) lua.FS.writeFile('/' + currentEditFile, inputEl.value);
        saveCache();
        updateFileList();
    }
    inputEl.value = savedCode;
    isEditingFile = false;
    currentEditFile = null;
    document.getElementById('saveBtn').style.display = 'none';
    document.getElementById('cancelEditBtn').style.display = 'none';
    updateLineNumbers();
}

/**
 * 取消编辑
 */
function cancelEdit() {
    inputEl.value = savedCode;
    isEditingFile = false;
    currentEditFile = null;
    document.getElementById('saveBtn').style.display = 'none';
    document.getElementById('cancelEditBtn').style.display = 'none';
    updateLineNumbers();
}

/**
 * 下载文件
 */
function downloadFile(name) {
    var content = fileCache[name];
    var a = document.createElement('a');
    a.href = 'data:text/plain;charset=utf-8,' + encodeURIComponent(content);
    a.download = name.split('/').pop();
    a.click();
}

/**
 * 删除文件
 */
function deleteFile(name) {
    if (confirm(t('delConfirm') + name + '?')) {
        // 检查是否正在编辑该文件
        if (isEditingFile && currentEditFile === name) {
            inputEl.value = '';
            isEditingFile = false;
            currentEditFile = null;
            document.getElementById('saveBtn').style.display = 'none';
            document.getElementById('cancelEditBtn').style.display = 'none';
            updateLineNumbers();
        }
        
        delete fileCache[name];
        if (lua) {
            try { lua.FS.unlink('/' + name); } catch (e) {
                try { lua.FS.rmdir('/' + name); } catch (e2) {}
            }
        }
        saveCache();
        updateFileList();
    }
}

/**
 * 重命名文件
 */
function renameFile(name) {
    var newName = prompt(t('newName'), name);
    if (newName && newName !== name) {
        fileCache[newName] = fileCache[name];
        delete fileCache[name];
        if (lua) {
            try { lua.FS.rename('/' + name, '/' + newName); } catch (e) {}
        }
        saveCache();
        updateFileList();
    }
}

/**
 * 上传文件
 */
function uploadFiles(files) {
    for (var i = 0; i < files.length; i++) {
        (function(file) {
            if (file.size > 5000000) {
                outputEl.textContent += t('fileTooLarge') + file.name + t('notPersist') + '\\n';
                outputEl.scrollTop = outputEl.scrollHeight;
            }
            var reader = new FileReader();
            reader.onload = function(e) {
                var content = e.target.result;
                fileCache[file.name] = content;
                if (lua) lua.FS.writeFile('/' + file.name, content);
                saveCache();
                updateFileList();
                outputEl.textContent += t('uploaded') + file.name + ' (' + formatSize(content.length) + ')\n';
                outputEl.scrollTop = outputEl.scrollHeight;
            };
            reader.readAsText(file);
        })(files[i]);
    }
    document.getElementById('fileInput').value = '';
}

/**
 * 创建目录
 */
function createDir() {
    var name = prompt(t('dirName'));
    if (name) {
        fileCache[name] = null;
        if (lua) {
            try { lua.FS.mkdir('/' + name); } catch (e) {}
        }
        saveCache();
        updateFileList();
    }
}

/**
 * 导出工作区
 */
function exportWorkspace() {
    var data = JSON.stringify(fileCache, null, 2);
    var a = document.createElement('a');
    a.href = 'data:application/json;charset=utf-8,' + encodeURIComponent(data);
    a.download = 'lxclua_workspace.json';
    a.click();
}

/**
 * 导入工作区
 */
function importWorkspace(file) {
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function(e) {
        try {
            var data = JSON.parse(e.target.result);
            fileCache = data;
            saveCache();
            if (lua) {
                var names = Object.keys(fileCache);
                for (var i = 0; i < names.length; i++) {
                    var name = names[i];
                    if (fileCache[name] === null) {
                        try { lua.FS.mkdir('/' + name); } catch (ex) {}
                    } else {
                        lua.FS.writeFile('/' + name, fileCache[name]);
                    }
                }
            }
            updateFileList();
            outputEl.textContent += t('importOk') + '\\n';
        } catch (ex) {
            outputEl.textContent += t('importFail') + ex + '\n';
        }
    };
    reader.readAsText(file);
    document.getElementById('importInput').value = '';
}
'''
    
    # HTML模板
    html_content = f'''<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LXCLUA WebAssembly</title>
    <style>{minify_css(css_code)}</style>
</head>
<body>
    <div class="header">
        <h2 id="pageTitle">LXCLUA WebAssembly</h2>
        <select id="langSelect" onchange="changeLang(this.value)">
            <option value="en">English</option>
            <option value="zh">中文</option>
        </select>
    </div>
    <div id="output">Loading...</div>
    <div id="editor-container">
        <div id="lineNumbers">1</div>
        <textarea id="input" rows="8"></textarea>
        <div id="editor-size">0B</div>
    </div>
    <br>
    <button id="runBtn" onclick="runLua()">Run</button>
    <button id="stopBtn" onclick="stopLua()">Stop</button>
    <button id="clearBtn" onclick="clearConsole()">Clear</button>
    <button id="copyBtn" onclick="copyOutput()">Copy</button>
    <button id="terminalBtn" onclick="toggleTerminalSize()">Expand Terminal</button>
    <span class="terminal-controls">
        <button onclick="changeTerminalSize(-50)">-</button>
        <button onclick="changeTerminalSize(50)">+</button>
    </span>
    <button id="uploadBtn" onclick="document.getElementById('fileInput').click()">Upload</button>
    <button id="newFileBtn" onclick="createFile()">New File</button>
    <button id="newDirBtn" onclick="createDir()">New Dir</button>
    <button id="saveBtn" onclick="saveAndExit()">Save</button>
    <button id="cancelEditBtn" onclick="cancelEdit()">Cancel</button>
    <span class="font-controls">
        <button onclick="changeFontSize(-2)">A-</button>
        <button onclick="changeFontSize(2)">A+</button>
        <select id="fontSelect" onchange="changeFont(this.value)">
            <option value="monospace">Monospace</option>
            <option value="Consolas, monospace">Consolas</option>
            <option value="'Courier New', monospace">Courier New</option>
            <option value="Monaco, monospace">Monaco</option>
            <option value="'Fira Code', monospace">Fira Code</option>
            <option value="'Source Code Pro', monospace">Source Code Pro</option>
        </select>
        <button id="fontBtn" onclick="document.getElementById('fontFileInput').click()">Font</button>
        <input type="file" id="fontFileInput" accept=".ttf,.otf,.woff,.woff2" onchange="uploadFont(this.files[0])">
    </span>
    <input type="file" id="fileInput" multiple accept=".lua,.txt" onchange="uploadFiles(this.files)">
    <div class="toolbar">
        <button id="exportBtn" onclick="exportWorkspace()">Export</button>
        <button id="importBtn" onclick="document.getElementById('importInput').click()">Import</button>
        <input type="file" id="importInput" accept=".json" onchange="importWorkspace(this.files[0])">
    </div>
    <div id="fileList"></div>
    <script>
{js_content}
    </script>
    <script>{minify_js(app_js)}</script>
</body>
</html>'''
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html_content)
    
    file_size = os.path.getsize(output_path) / 1024
    print(f'生成完成: {output_path}')
    print(f'文件大小: {file_size:.1f} KB')

if __name__ == '__main__':
    embed_js_to_html()
