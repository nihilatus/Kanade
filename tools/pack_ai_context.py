import zipfile
import os
from datetime import datetime

def main():
    # 產生帶有當下年月日與時分的檔名，例如: Kanade_AI_Context_20261111_1530.zip
    timestamp = datetime.now().strftime('%Y%m%d_%H%M')
    zip_filename = f'Kanade_AI_Context_{timestamp}.zip'

    # Align with .cursorignore: drop build cache, local git dumps, and binary wasm.
    # Keep data/chord_map.machine.csv — small and useful for chord work.
    exclude_dirs = {'.git', '.pio', '.vscode', 'node_modules', '__pycache__', 'build'}
    exclude_files = {'secrets.h'}
    exclude_paths = {
        'git_history.txt',
        'tools/git_history.txt',
        'web/chord_core.wasm',
    }

    print(f'正在建立 AI 專案上下文壓縮檔: {zip_filename} ...')

    with zipfile.ZipFile(zip_filename, 'w', zipfile.ZIP_DEFLATED) as zipf:
        count = 0
        for root, dirs, files in os.walk('.'):
            dirs[:] = [d for d in dirs if d not in exclude_dirs]
            for file in files:
                if file in exclude_files:
                    continue
                if file.startswith('Kanade_AI_Context') and file.endswith('.zip'):
                    continue

                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, '.').replace('\\', '/')

                if rel_path in exclude_paths:
                    continue
                if 'secret' in file.lower():
                    continue

                zipf.write(full_path, rel_path)
                print(f' [✓] 已加入: {rel_path}')
                count += 1

    print(f'\n打包完成！共收錄 {count} 個項目，已儲存為：{zip_filename}')

if __name__ == '__main__':
    main()
