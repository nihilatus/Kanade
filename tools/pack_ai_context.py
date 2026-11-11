import zipfile
import os
import subprocess
from datetime import datetime

def main():
    # 產生帶有當下年月日與時分的檔名，例如: Kanade_AI_Context_20261111_1530.zip
    timestamp = datetime.now().strftime('%Y%m%d_%H%M')
    zip_filename = f'Kanade_AI_Context_{timestamp}.zip'

    exclude_dirs = {'.git', '.pio', '.vscode', 'node_modules', '__pycache__', 'build'}
    exclude_files = {'secrets.h'}

    print(f'正在建立 AI 專案上下文壓縮檔: {zip_filename} ...')

    with zipfile.ZipFile(zip_filename, 'w', zipfile.ZIP_DEFLATED) as zipf:
        # 1. 抓取並寫入 Git Commit 歷史說明
        try:
            git_log = subprocess.check_output(
                ['git', 'log', '--pretty=format:%h - %an, %ar : %s'],
                encoding='utf-8',
                errors='ignore'
            )
            zipf.writestr('git_history.txt', git_log)
            print(' [✓] 已加入: git_history.txt (Git Commit 歷史說明)')
        except Exception as e:
            print(sw := f' [!] 無法取得 Git 歷史: {e}')

        # 2. 走訪並加入專案所有檔案（排除指定目錄與檔案）
        count = 0
        for root, dirs, files in os.walk('.'):
            dirs[:] = [d for d in dirs if d not in exclude_dirs]
            for file in files:
                if file in exclude_files:
                    continue
                if file.startswith('Kanade_AI_Context') and file.endswith('.zip'):
                    continue
                
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, '.')
                
                if 'secret' in file.lower():
                    continue

                zipf.write(full_path, rel_path)
                print(f' [✓] 已加入: {rel_path}')
                count += 1

    print(f'\n打包完成！共收錄 {count + 1} 個項目（含 git_history.txt），已儲存為：{zip_filename}')

if __name__ == '__main__':
    main()
