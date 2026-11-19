#!/usr/bin/env python3
"""按文件清单并发拉取 LVGL v8.3.11 源码 (整包 zip 下载不稳定时的可靠通道)。

用法:
    python tools/fetch_lvgl_files.py --output third_party/lvgl

只下载编译所需的 379 个文件 (src/** + lvgl.h 等), 总量约 6 MB,
远小于约 20 MB 的整包 zip。可重复运行, 已存在且非空的文件会跳过。
"""

import argparse
import json
import os
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

TAG = "v8.3.11"
RAW = f"https://raw.githubusercontent.com/lvgl/lvgl/{TAG}/"
TREE_API = f"https://api.github.com/repos/lvgl/lvgl/git/trees/{TAG}?recursive=1"
KEEP_PREFIXES = ("src/",)
KEEP_EXACT = {"lvgl.h", "lv_conf_template.h", "LICENSE.txt", "README.md"}


def wanted(path: str) -> bool:
    return path.startswith(KEEP_PREFIXES) or path in KEEP_EXACT


def fetch_one(path: str, destination: Path, retries: int = 4) -> tuple:
    target = destination / path
    if target.exists():
        return path, True, "cached"
    target.parent.mkdir(parents=True, exist_ok=True)
    url = RAW + path
    last_error = ""
    for attempt in range(retries):
        try:
            request = urllib.request.Request(
                url, headers={"User-Agent": "workbuddy-gd32-ota/1.0"})
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read()
            # 仓库里存在空文件 (例如某些 README.md), 空内容也是合法结果
            target.write_bytes(data)
            return path, True, f"{len(data)}B"
        except Exception as error:  # noqa: BLE001 - 网络错误统一重试
            last_error = f"{type(error).__name__}: {error}"
            time.sleep(0.6 * (attempt + 1))
    return path, False, last_error


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("third_party/lvgl"))
    parser.add_argument("--tree-json", type=Path,
                        help="可选: 已缓存的 GitHub tree JSON, 避免再请求 API")
    parser.add_argument("--workers", type=int, default=12)
    args = parser.parse_args()

    if args.tree_json and args.tree_json.exists():
        tree = json.loads(args.tree_json.read_text(encoding="utf-8"))
    else:
        print("Fetching LVGL file list from GitHub API ...", flush=True)
        request = urllib.request.Request(
            TREE_API, headers={"User-Agent": "workbuddy-gd32-ota/1.0"})
        with urllib.request.urlopen(request, timeout=40) as response:
            tree = json.loads(response.read().decode("utf-8"))

    paths = sorted({entry["path"] for entry in tree.get("tree", [])
                    if entry.get("type") == "blob" and wanted(entry["path"])})
    if not paths:
        print("ERROR: no files selected", file=sys.stderr)
        raise SystemExit(1)
    print(f"Selected {len(paths)} files -> {args.output}", flush=True)

    failed = []
    done = 0
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(fetch_one, p, args.output): p for p in paths}
        for future in as_completed(futures):
            path, ok, info = future.result()
            done += 1
            if not ok:
                failed.append((path, info))
            if done % 40 == 0 or done == len(paths):
                print(f"  {done}/{len(paths)} files", flush=True)

    if failed:
        print(f"FAILED {len(failed)} files:", file=sys.stderr)
        for path, info in failed[:20]:
            print(f"  {path}: {info}", file=sys.stderr)
        raise SystemExit(2)
    print(f"LVGL {TAG} ready at {args.output} ({len(paths)} files)", flush=True)


if __name__ == "__main__":
    main()
