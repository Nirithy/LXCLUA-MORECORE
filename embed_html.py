#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将lxclua.js内嵌到HTML中生成单文件HTML

功能描述：读取lxclua.js和lxclua.html模板，将JS内嵌生成单文件lxclua_standalone.html
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
    
    css_code = '''body{font-family:monospace;padding:10px;background:#1e1e1e;color:#d4d4d4}#output{background:#0d0d0d;padding:10px;border-radius:4px;white-space:pre-wrap;min-height:200px;overflow:auto}#input{width:100%;padding:8px;margin-top:10px;background:#252526;color:#d4d4d4;border:1px solid #3c3c3c;box-sizing:border-box}button{padding:8px 16px;margin-top:10px;background:#0e639c;color:white;border:none;cursor:pointer;margin-right:5px}button:active{background:#094771}#fileList{margin-top:10px;background:#252526;padding:10px;border-radius:4px}.file-item{display:flex;justify-content:space-between;align-items:center;padding:4px 8px;margin:2px 0;background:#1e1e1e;border-radius:2px;flex-wrap:wrap}.file-item span{color:#9cdcfe;flex:1;min-width:100px}.file-item .dir{color:#dcdcaa}.file-item button{padding:2px 8px;margin:0 2px;font-size:12px}.file-item .del{background:#c53030}.file-item .mov{background:#6b7280}.file-item .ren{background:#d97706}.file-item .dl{background:#059669}.file-item .edit{background:#7c3aed}#fileInput,#importInput{display:none}.toolbar{margin-top:10px}.toolbar button{background:#374151}#saveBtn{background:#10b981;display:none}#cancelEditBtn{background:#ef4444;display:none}'''
    
    app_js = '''var lua=null;var outputEl=document.getElementById('output');var fileCache={};var savedCode='';var isEditingFile=false;var currentEditFile=null;function loadCache(){try{var saved=localStorage.getItem('lxclua_files');if(saved)fileCache=JSON.parse(saved);}catch(e){}}function saveCache(){try{localStorage.setItem('lxclua_files',JSON.stringify(fileCache));}catch(e){}}function restoreFilesToFS(){var names=Object.keys(fileCache);for(var i=0;i<names.length;i++){var name=names[i];if(fileCache[name]===null){try{lua.FS.mkdir('/'+name);}catch(e){}}else{var parts=name.split('/');if(parts.length>1){var dir=parts.slice(0,-1).join('/');try{lua.FS.mkdir('/'+dir);}catch(e){}}lua.FS.writeFile('/'+name,fileCache[name]);}}}loadCache();var Module={print:function(text){outputEl.textContent+=text+'\\n';},printErr:function(text){outputEl.textContent+='[ERR] '+text+'\\n';}};LuaModule(Module).then(function(m){lua=m;restoreFilesToFS();updateFileList();outputEl.textContent='LXCLUA Ready!\\n';m.callMain(['-v']);});function runLua(){if(!lua)return;outputEl.textContent+='\\n--- Run ---\\n';var code=document.getElementById('input').value;lua.FS.writeFile('/tmp.lua',code);lua.callMain(['/tmp.lua']);syncFromFS();}function isDirectory(mode){return(mode&61440)===16384;}function syncFromFS(){try{var files=lua.FS.readdir('/');for(var i=0;i<files.length;i++){var name=files[i];if(name==='.'||name==='..'||name==='tmp'||name==='home'||name==='dev'||name==='proc'||name==='tmp.lua')continue;try{var stat=lua.FS.stat('/'+name);if(isDirectory(stat.mode)){if(!(name in fileCache)){fileCache[name]=null;}syncDirFromFS(name);}else{var content=lua.FS.readFile('/'+name,{encoding:'utf8'});fileCache[name]=content;}}catch(e){}}saveCache();updateFileList();}catch(e){}}function syncDirFromFS(dir){try{var files=lua.FS.readdir('/'+dir);for(var i=0;i<files.length;i++){var name=files[i];if(name==='.'||name==='..')continue;var fullPath=dir+'/'+name;try{var stat=lua.FS.stat('/'+fullPath);if(isDirectory(stat.mode)){if(!(fullPath in fileCache)){fileCache[fullPath]=null;}syncDirFromFS(fullPath);}else{var content=lua.FS.readFile('/'+fullPath,{encoding:'utf8'});fileCache[fullPath]=content;}}catch(e){}}}catch(e){}}function uploadFiles(files){for(var i=0;i<files.length;i++){var file=files[i];var reader=new FileReader();reader.onload=(function(f){return function(e){var content=e.target.result;var name=f.name;fileCache[name]=content;saveCache();if(lua){lua.FS.writeFile('/'+name,content);}updateFileList();};})(file);reader.readAsText(file);}document.getElementById('fileInput').value='';}function updateFileList(){var listEl=document.getElementById('fileList');var names=Object.keys(fileCache);if(names.length===0){listEl.innerHTML='';return;}var html='<b>Files:</b>';for(var i=0;i<names.length;i++){var name=names[i];var isDir=fileCache[name]===null;var cls=isDir?'dir':'';var icon=isDir?'[D] ':'';var escapedName=name.replace(/'/g,"\\\\'");html+='<div class="file-item"><span class="'+cls+'">'+icon+name+'</span>';html+='<button class="del" onclick="removeFile(\\''+escapedName+'\\')">X</button>';html+='<button class="ren" onclick="renameFile(\\''+escapedName+'\\')">Ren</button>';if(!isDir){html+='<button class="edit" onclick="editFile(\\''+escapedName+'\\')">Edit</button>';html+='<button class="dl" onclick="downloadFile(\\''+escapedName+'\\')">DL</button>';html+='<button class="mov" onclick="moveFile(\\''+escapedName+'\\')">Move</button>';}html+='</div>';}listEl.innerHTML=html;}function removeFile(name){delete fileCache[name];saveCache();if(lua){try{lua.FS.unlink('/'+name);}catch(e){}try{lua.FS.rmdir('/'+name);}catch(e){}}updateFileList();}function createDir(){var name=prompt('Directory name:');if(!name)return;fileCache[name]=null;saveCache();if(lua){try{lua.FS.mkdir('/'+name);}catch(e){}}updateFileList();}function moveFile(name){var dirs=[];var names=Object.keys(fileCache);for(var i=0;i<names.length;i++){if(fileCache[names[i]]===null)dirs.push(names[i]);}if(dirs.length===0){alert('No directories. Create one first.');return;}var target=prompt('Move to directory ('+dirs.join(', ')+'):');if(!target||!dirs.includes(target))return;var content=fileCache[name];var newName=target+'/'+name.split('/').pop();delete fileCache[name];fileCache[newName]=content;saveCache();if(lua){try{lua.FS.unlink('/'+name);}catch(e){}lua.FS.writeFile('/'+newName,content);}updateFileList();}function editFile(name){var content=fileCache[name];if(content===null)return;savedCode=document.getElementById('input').value;isEditingFile=true;currentEditFile=name;document.getElementById('input').value=content;document.getElementById('input').focus();document.getElementById('saveBtn').style.display='inline-block';document.getElementById('cancelEditBtn').style.display='inline-block';outputEl.textContent+='Editing: '+name+'\\n';}function saveAndExit(){if(!isEditingFile||!currentEditFile)return;var content=document.getElementById('input').value;fileCache[currentEditFile]=content;saveCache();if(lua){lua.FS.writeFile('/'+currentEditFile,content);}outputEl.textContent+='Saved: '+currentEditFile+'\\n';document.getElementById('input').value=savedCode;document.getElementById('saveBtn').style.display='none';document.getElementById('cancelEditBtn').style.display='none';isEditingFile=false;currentEditFile=null;savedCode='';}function cancelEdit(){if(!isEditingFile)return;document.getElementById('input').value=savedCode;document.getElementById('saveBtn').style.display='none';document.getElementById('cancelEditBtn').style.display='none';outputEl.textContent+='Edit cancelled\\n';isEditingFile=false;currentEditFile=null;savedCode='';}function downloadFile(name){var content=fileCache[name];if(content===null)return;var fileName=name.split('/').pop();var a=document.createElement('a');a.href='data:text/plain;charset=utf-8,'+encodeURIComponent(content);a.download=fileName;a.style.display='none';document.body.appendChild(a);a.click();document.body.removeChild(a);}function renameFile(name){var newName=prompt('New name:',name);if(!newName||newName===name)return;var content=fileCache[name];delete fileCache[name];fileCache[newName]=content;saveCache();if(lua){try{lua.FS.unlink('/'+name);}catch(e){}try{lua.FS.rmdir('/'+name);}catch(e){}if(content===null){try{lua.FS.mkdir('/'+newName);}catch(e){}}else{lua.FS.writeFile('/'+newName,content);}}updateFileList();}function exportWorkspace(){var data=JSON.stringify(fileCache,null,2);var a=document.createElement('a');a.href='data:application/json;charset=utf-8,'+encodeURIComponent(data);a.download='lxclua_workspace.json';a.style.display='none';document.body.appendChild(a);a.click();document.body.removeChild(a);}function importWorkspace(file){if(!file)return;var reader=new FileReader();reader.onload=function(e){try{var data=JSON.parse(e.target.result);var names=Object.keys(data);for(var i=0;i<names.length;i++){var name=names[i];fileCache[name]=data[name];if(lua){if(data[name]===null){try{lua.FS.mkdir('/'+name);}catch(e){}}else{var parts=name.split('/');if(parts.length>1){var dir=parts.slice(0,-1).join('/');try{lua.FS.mkdir('/'+dir);}catch(e){}}lua.FS.writeFile('/'+name,data[name]);}}}saveCache();updateFileList();outputEl.textContent+='Imported '+names.length+' files\\n';}catch(e){alert('Invalid JSON file');}};reader.readAsText(file);document.getElementById('importInput').value='';}document.addEventListener('keydown',function(e){if(e.ctrlKey&&e.key==='s'){e.preventDefault();if(isEditingFile){saveAndExit();}}});'''
    
    html_content = f'''<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>LXCLUA WebAssembly</title><style>{css_code}</style></head><body><h2>LXCLUA WebAssembly</h2><div id="output">Loading...</div><textarea id="input" rows="8" placeholder="print('Hello LXCLUA!')"></textarea><br><button onclick="runLua()">Run</button><button onclick="document.getElementById('fileInput').click()">Upload</button><button onclick="createDir()">New Dir</button><button id="saveBtn" onclick="saveAndExit()">Save</button><button id="cancelEditBtn" onclick="cancelEdit()">Cancel</button><input type="file" id="fileInput" multiple accept=".lua,.txt" onchange="uploadFiles(this.files)"><div class="toolbar"><button onclick="exportWorkspace()">Export</button><button onclick="document.getElementById('importInput').click()">Import</button><input type="file" id="importInput" accept=".json" onchange="importWorkspace(this.files[0])"></div><div id="fileList"></div><script>
{js_content}
</script><script>{app_js}</script></body></html>'''
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html_content)
    
    file_size = os.path.getsize(output_path) / 1024
    print(f'生成完成: {output_path}')
    print(f'文件大小: {file_size:.1f} KB')

if __name__ == '__main__':
    embed_js_to_html()
