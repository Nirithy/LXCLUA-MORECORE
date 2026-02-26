#!/usr/bin/env python3
import os

def build_site():
    root_dir = os.getcwd()
    src_dir = os.path.join(root_dir, 'website', 'src')
    output_file = os.path.join(root_dir, 'lxclua_official_site.html')

    print(f"Building website from {src_dir}...")

    # Read HTML
    with open(os.path.join(src_dir, 'index.html'), 'r', encoding='utf-8') as f:
        html_content = f.read()

    # Read CSS
    css_path = os.path.join(src_dir, 'style.css')
    with open(css_path, 'r', encoding='utf-8') as f:
        css_content = f.read()

    # Read JS
    js_path = os.path.join(src_dir, 'script.js')
    with open(js_path, 'r', encoding='utf-8') as f:
        js_content = f.read()

    # Inline CSS
    # Replace <link rel="stylesheet" href="style.css">
    # Note: We match the exact string used in index.html or use a regex if needed.
    # For simplicity, we'll try strict replacement first.
    style_tag = f'<style>\n{css_content}\n</style>'
    html_content = html_content.replace('<link rel="stylesheet" href="style.css">', style_tag)

    # Inline JS
    # Replace <script src="script.js"></script>
    script_tag = f'<script>\n{js_content}\n</script>'
    html_content = html_content.replace('<script src="script.js"></script>', script_tag)

    # Write Output
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(html_content)

    print(f"Successfully built single-file website: {output_file}")
    print(f"Size: {os.path.getsize(output_file) / 1024:.2f} KB")

if __name__ == '__main__':
    build_site()