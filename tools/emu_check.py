"""QEMU・Wokwi のシリアルのログを判定する（P7 の決定 #119・#123・#125。tools/qemu.sh・tools/wokwi.sh から呼ぶ）。

ログの "I (<ms>) node: {...}" の行（ESP_LOG に出す JSON。ホスト版の JSON Lines と同じ項目）を読む。

    python3 tools/emu_check.py boot <log>   起動・4つのタスク・BOOT（power_on）・stats
    python3 tools/emu_check.py soak <log>   NFR-03：起動から5分後と終わりで、ヒープの最低残量が下がっていない
    python3 tools/emu_check.py heap <log>   Wi-Fi に接続した後の stats で、ヒープの最低残量 30KB 以上・各タスクのスタックの残り 25% 以上
"""

from __future__ import annotations

import json
import re
import sys

LINE = re.compile(r"^[IWE] \((\d+)\) node: (\{.*\})\s*$")
TASKS = ("control", "sense", "comm", "ui")
HEAP_MIN_BYTES = 30 * 1024  # #114：Wi-Fi・MQTT が動いた後のヒープの最低残量
STACK_BYTES = {"control": 8192, "sense": 4096, "comm": 6144, "ui": 4096}  # #97・#124（esp32_tasks.cpp と同じ）


def records(path: str) -> list[tuple[int, dict]]:
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE.match(line.strip())
            if m:
                try:
                    out.append((int(m.group(1)), json.loads(m.group(2))))
                except ValueError:
                    pass
    return out


class Check:
    def __init__(self) -> None:
        self.failed = 0

    def __call__(self, ok: bool, what: str) -> None:
        print(f"[{'合格' if ok else '不合格'}] {what}")
        if not ok:
            self.failed += 1


def check_stacks(c: Check, stats: dict) -> None:
    free = stats.get("stack_free_min", {})
    for t in TASKS:
        ratio = free.get(t, 0) / STACK_BYTES[t]
        c(ratio >= 0.25, f"{t} のスタックの残りが 25% 以上（{free.get(t)}／{STACK_BYTES[t]} バイト、{ratio:.0%}）")


def check_heap(path: str) -> int:
    c = Check()
    got_ip = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^I \((\d+)\) esp_netif_handlers: sta ip:", line.strip())
            if m and got_ip is None:
                got_ip = int(m.group(1))
    c(got_ip is not None, f"Wi-Fi に接続した（sta ip。{got_ip} ms）")
    after = [r for t, r in records(path) if r.get("ev") == "stats" and got_ip is not None and t > got_ip]
    c(len(after) >= 1, f"接続した後の stats がある（{len(after)} 件）")
    if after:
        last = after[-1]
        c(last["heap_min"] >= HEAP_MIN_BYTES, f"ヒープの最低残量が {HEAP_MIN_BYTES} バイト以上（heap_min {last['heap_min']}、heap_free {last['heap_free']}）")
        check_stacks(c, last)
    return 1 if c.failed else 0


def check_boot(path: str) -> int:
    rec = records(path)
    c = Check()
    boots = [r for _, r in rec if r.get("ev") == "boot"]
    c(len(boots) >= 1, "起動して boot のログが出る")
    if boots:
        b = boots[0]
        c(b.get("reset_reason") == "power_on" and b.get("platform_reason") == "ESP_RST_POWERON",
          f"BOOT の reset_reason が power_on、platform_reason が ESP_RST_POWERON（{b.get('reset_reason')}・{b.get('platform_reason')}）")
        c(re.fullmatch(r"[0-9a-f]{8}", b.get("boot_id", "")) is not None, f"boot_id が16進小文字8桁（{b.get('boot_id')}）")
    stats = [(t, r) for t, r in rec if r.get("ev") == "stats"]
    c(len(stats) >= 2, f"stats のログが起動の直後と1分後に出る（{len(stats)} 件）")
    if stats:
        last = stats[-1][1]
        free = last.get("stack_free_min", {})
        c(all(free.get(t, 0) > 0 for t in TASKS), f"4つのタスクのスタックの残りが出る（{free}）")
        check_stacks(c, last)
        print(f"  heap_free={last.get('heap_free')} heap_min={last.get('heap_min')}")
    diags = [r for _, r in rec if r.get("ev") == "diag"]
    if len(diags) >= 2:
        first, lastd = diags[0], diags[-1]
        # 4つのタスクが動いている：control（diag を出す）、sense（計測の失敗を数える）、ui（表示の失敗を返す）、comm（接続の試み）
        c(lastd.get("read_errors", 0) > first.get("read_errors", 0), "sense が動いている（計測の結果が control に届く）")
        c(lastd.get("display_errors", 0) > first.get("display_errors", 0), "ui が動いている（表示の結果が control に届く）")
        c(any(r.get("ev") == "connect" for _, r in rec), "comm が動いている（MQTT の接続の依頼を処理する）")
    else:
        c(False, "diag のログが2件以上ある")
    return 1 if c.failed else 0


def check_soak(path: str) -> int:
    rec = records(path)
    c = Check()
    stats = [(t, r) for t, r in rec if r.get("ev") == "stats"]
    after5 = [r for t, r in stats if t >= 5 * 60 * 1000]
    c(len(after5) >= 2, f"5分後より後の stats が2件以上ある（{len(after5)} 件）")
    tx = [r for _, r in rec if r.get("ev") == "tx" and r.get("topic", "").endswith("/telemetry")]
    c(len(tx) >= 60, f"MQTT につながり telemetry を送り続けている（{len(tx)} 件）")
    if len(after5) >= 2:
        a, b = after5[0], after5[-1]
        c(b["heap_min"] >= a["heap_min"],
          f"ヒープの最低残量が下がっていない（5分後 {a['heap_min']} → 終わり {b['heap_min']} バイト）")
        for t, r in stats:
            print(f"  {t / 60000:5.1f} 分 heap_free={r['heap_free']} heap_min={r['heap_min']} stack={r['stack_free_min']}")
    return 1 if c.failed else 0


if __name__ == "__main__":
    checks = {"boot": check_boot, "soak": check_soak, "heap": check_heap}
    if len(sys.argv) != 3 or sys.argv[1] not in checks:
        print(__doc__)
        sys.exit(2)
    sys.exit(checks[sys.argv[1]](sys.argv[2]))
