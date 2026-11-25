# /// script
# requires-python = ">=3.10"
# dependencies = ["paho-mqtt>=2.1,<3"]
# ///
"""ブローカーの停止・再開の試験（P4、NFR-08・FR-08・FR-11、open-questions #70・#77）。

Mosquitto を docker compose stop／start で止めて戻し、observer で購読した電文から次を確かめる。

1. 停止の前に state（online）が届く
2. 停止中に起こしたイベント（--sim-fault で土壌水分センサーを故障させる）が、再接続後に届く
3. 再接続後に、同じ boot_id の state（online）→ UPSTREAM_LOST → 停止中のイベント → UPSTREAM_RESTORED の順で届く。
   RESTORED の detail は down_ms・dropped
4. ブローカーが戻ってから60秒以内に再接続する（秒数を表示する）
5. 最後にホスト版を SIGKILL で止め、LWT の offline の state（retain、boot_id が同じ、seq は再接続時に登録した値）が出る

ブローカー自身を止めると LWT は出せない（#77）ので、LWT は 5 で確かめる。

取りこぼし対策（open-questions #86・#87）：本体の再接続はジッターなしで、切れてから 1・3・7・15・31・63 秒後に試す（#69）。
止めておく時間をこの予定から選び、ブローカーを戻してから本体の次の試行まで5秒以上空ける。observer はブローカーを戻したら
100ms ごとに接続を試し、本体の次の試行より前に購読し直せたことを確かめてから判定に進む。間に合わなければ環境の要因として失敗にする。

使い方（リポジトリの直下で）：
    IOTSPEC_MQTT_PASSWORD=iotspec-dev uv run tests/system/broker_restart.py [--binary ./build/host/mcu_cpp_01]
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from urllib.parse import urlparse

import paho.mqtt.client as mqtt

DEVICE_ID = "mcu-cpp-01"
COMPOSE_FILE = "external/iot-common-spec/docker-compose.yml"
RECONNECT_LIMIT_S = 60.0
# 本体が切れてから MQTT の接続を試す時刻（秒）。待ちは 1・2・4・8・16・32・60 秒（#69・#87）
RETRY_SCHEDULE_S = (1.0, 3.0, 7.0, 15.0, 31.0, 63.0, 123.0)
# ブローカーを戻してから本体の次の試行までに空ける時間
MIN_GAP_S = 5.0
# 前の試行がブローカーの起動と重ならないための余裕
MIN_AFTER_PREV_S = 1.0


@dataclass
class Received:
    at: float  # 受けた時刻（time.monotonic）
    kind: str  # state・event・telemetry・cmd/ack
    retain: bool
    message: dict


@dataclass
class Observer:
    """observer で iot/v1/<site>/mcu-cpp-01/# を購読し、受けた電文を順に残す。"""

    host: str
    port: int
    password: str
    site: str
    received: list[Received] = field(default_factory=list)
    lock: threading.Lock = field(default_factory=threading.Lock)
    connected: threading.Event = field(default_factory=threading.Event)
    subscribed: threading.Event = field(default_factory=threading.Event)
    disconnected_at: float | None = None  # 最後に切れた時刻（time.monotonic）
    subscribed_at: float | None = None  # 最後に購読できた時刻（time.monotonic）

    def __post_init__(self) -> None:
        self.prefix = f"iot/v1/{self.site}/{DEVICE_ID}/"
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"observer-broker-restart-{os.getpid()}")
        self.client.username_pw_set("observer", self.password)
        # ブローカーが戻ったら 100ms ごとに接続を試す（本体より先に購読し直すため。#86）
        self.client.reconnect_delay_set(min_delay=0.1, max_delay=0.1)  # type: ignore[arg-type]
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_subscribe = self._on_subscribe
        self.client.on_message = self._on_message

    def start(self) -> None:
        self.client.connect(self.host, self.port, keepalive=10)
        self.client.loop_start()
        if not self.subscribed.wait(10):
            raise RuntimeError("observer がブローカーに接続できない")

    def stop(self) -> None:
        self.client.loop_stop()
        self.client.disconnect()

    def _on_connect(self, client, _userdata, _flags, reason_code, _properties) -> None:
        if reason_code.is_failure:
            return
        client.subscribe(self.prefix + "#", qos=1)
        self.connected.set()

    def _on_disconnect(self, _client, _userdata, _flags, _reason_code, _properties) -> None:
        self.connected.clear()
        self.subscribed.clear()
        self.disconnected_at = time.monotonic()

    def _on_subscribe(self, _client, _userdata, _mid, _reason_codes, _properties) -> None:
        self.subscribed_at = time.monotonic()
        self.subscribed.set()

    def _on_message(self, _client, _userdata, msg) -> None:
        try:
            body = json.loads(msg.payload)
        except ValueError:
            return
        with self.lock:
            self.received.append(Received(time.monotonic(), msg.topic[len(self.prefix):], bool(msg.retain), body))

    def snapshot(self) -> list[Received]:
        with self.lock:
            return list(self.received)


def compose(*args: str) -> None:
    subprocess.run(["docker", "compose", "-f", COMPOSE_FILE, *args], check=True, stdout=subprocess.DEVNULL)


def wait_for(pred, timeout: float, step: float = 0.05):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        r = pred()
        if r:
            return r
        time.sleep(step)
    return None


def plan_outage(down: float) -> float:
    """止めておく時間から、本体が次に接続を試す時刻（切れてからの秒）を求める。条件に合わなければ例外。"""
    prev = max((t for t in RETRY_SCHEDULE_S if t <= down), default=0.0)
    nxt = min(t for t in RETRY_SCHEDULE_S if t > down)
    if nxt - down < MIN_GAP_S or (prev > 0 and down - prev < MIN_AFTER_PREV_S):
        raise SystemExit(
            f"--down {down:g} は使えない：前の試行 {prev:g} 秒・次の試行 {nxt:g} 秒。"
            f"ブローカーを戻してから次の試行まで {MIN_GAP_S:g} 秒以上空く値を選ぶ（例：20）"
        )
    return nxt


def code(r: Received) -> str:
    return r.message.get("payload", {}).get("code", "")


def status(r: Received) -> str:
    return r.message.get("payload", {}).get("status", "")


class Check:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def __call__(self, ok: bool, what: str) -> bool:
        print(f"[{'合格' if ok else '不合格'}] {what}")
        if not ok:
            self.failures.append(what)
        return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="./build/host/mcu_cpp_01")
    ap.add_argument("--site", default="home")
    ap.add_argument("--log", default="broker-restart-log.jsonl", help="ホスト版のログ（JSON Lines）")
    ap.add_argument("--stop-at", type=float, default=5.0, help="ホスト版の起動からブローカーを止めるまでの秒数")
    ap.add_argument("--fault-at", type=float, default=8.0, help="ホスト版の起動から土壌水分センサーを故障させるまでの秒数")
    ap.add_argument("--down", type=float, default=20.0,
                    help="ブローカーを止めておく秒数（本体が切れてから数える。次の試行まで5秒以上空く値）")
    args = ap.parse_args()
    next_attempt = plan_outage(args.down)

    password = os.environ.get("IOTSPEC_MQTT_PASSWORD")
    if not password:
        print("IOTSPEC_MQTT_PASSWORD を設定してください", file=sys.stderr)
        return 2
    url = urlparse(os.environ.get("IOTSPEC_MQTT_URL", "mqtt://localhost:1883"))
    host, port = url.hostname or "localhost", url.port or 1883

    check = Check()
    obs = Observer(host, port, password, args.site)
    obs.start()
    time.sleep(0.5)  # 前の実行の retain を受け流す

    t0 = time.monotonic()
    dev = subprocess.Popen(
        [args.binary, "--site", args.site, "--log", args.log, "--sim-fault", f"soil_moisture@{args.fault_at:g}"],
        env=os.environ.copy(),
    )
    try:
        # 1. 停止の前の state（online）。retain で届いた前の実行のものは除く
        first = wait_for(
            lambda: next((r for r in obs.snapshot() if r.kind == "state" and not r.retain and status(r) == "online"), None),
            10,
        )
        if not check(first is not None, "停止の前に state（online）が届く"):
            return 1
        boot_id = first.message["boot_id"]
        print(f"  boot_id={boot_id} seq={first.message['seq']}")

        def mine(r: Received) -> bool:
            return r.message.get("boot_id") == boot_id

        # BOOT を受けてから止める
        check(wait_for(lambda: any(mine(r) and code(r) == "BOOT" for r in obs.snapshot()), 10) is not None, "BOOT が届く")
        time.sleep(max(0.0, t0 + args.stop_at - time.monotonic()))

        # 2. ブローカーを止める → 故障を起こす → 戻す
        print(f"ブローカーを止める（起動から {time.monotonic() - t0:.1f} 秒）")
        compose("stop", "mosquitto")
        stopped_count = len(obs.snapshot())
        # 本体が切れた時刻は、observer が切れた時刻で見積もる（同じブローカーから同時に切られる）
        t_lost = obs.disconnected_at if obs.disconnected_at is not None else time.monotonic()
        time.sleep(max(0.0, t_lost + args.down - time.monotonic()))
        print(f"ブローカーを戻す（切れてから {time.monotonic() - t_lost:.1f} 秒。本体の次の試行は {next_attempt:g} 秒）")
        t_start_cmd = time.monotonic()
        compose("start", "mosquitto")
        t_start = time.monotonic()  # docker compose start が戻った時刻（再接続までの秒数はここから数える）
        deadline = t_lost + next_attempt
        if not obs.subscribed.wait(max(0.0, deadline - time.monotonic())) or (obs.subscribed_at or 1e18) >= deadline:
            print(f"[環境の要因] observer が本体の次の試行（切れてから {next_attempt:g} 秒）までに購読し直せなかった。"
                  "Docker・Mosquitto の起動の遅れを確かめてから流し直す", file=sys.stderr)
            return 3
        print(f"  observer は docker compose start から {obs.subscribed_at - t_start_cmd:.1f} 秒で購読し直した"
              f"（本体の次の試行の {deadline - obs.subscribed_at:.1f} 秒前）")

        restored = wait_for(
            lambda: next((r for r in obs.snapshot()[stopped_count:] if mine(r) and code(r) == "UPSTREAM_RESTORED"), None),
            RECONNECT_LIMIT_S + 30,
        )
        after = [r for r in obs.snapshot()[stopped_count:] if mine(r) and r.kind in ("state", "event")]
        online = next((r for r in after if r.kind == "state" and status(r) == "online"), None)

        # 4. 再接続までの秒数（ブローカーが戻ってから state（online）を受けるまで）
        if online is not None:
            secs = online.at - t_start
            print(f"  ブローカーが戻ってから再接続（state online）まで {secs:.1f} 秒")
            check(secs <= RECONNECT_LIMIT_S, f"ブローカーが戻ってから {RECONNECT_LIMIT_S:.0f} 秒以内に再接続する")
        else:
            check(False, "再接続後に state（online）が届く")
        if online is not None:
            check(online.message["seq"] == first.message["seq"] + 1, "再接続後の state は同じ boot_id で seq が次の値")
        check(restored is not None, "UPSTREAM_RESTORED が届く")

        # 3. 順序：state（online）→ LOST → 停止中のイベント → RESTORED
        seq_codes = [("state:" + status(r)) if r.kind == "state" else code(r) for r in after]
        print(f"  再接続後に届いた順：{seq_codes}")
        try:
            i_online = seq_codes.index("state:online")
            i_lost = seq_codes.index("UPSTREAM_LOST")
            i_restored = seq_codes.index("UPSTREAM_RESTORED")
            check(i_online < i_lost < i_restored, "state（online）→ UPSTREAM_LOST → UPSTREAM_RESTORED の順")
            between = after[i_lost + 1 : i_restored]
            fault = next((r for r in between if code(r) == "SENSOR_FAULT"), None)
            check(fault is not None and fault.message["payload"].get("channel") == "soil",
                  "停止中に起こした SENSOR_FAULT（soil）が LOST と RESTORED の間に届く")
            lost = after[i_lost]
            check(lost.message["payload"].get("severity") == "warning", "UPSTREAM_LOST は warning")
            rest = after[i_restored]
            detail = rest.message["payload"].get("detail", {})
            check(rest.message["payload"].get("severity") == "info", "UPSTREAM_RESTORED は info")
            check(isinstance(detail.get("down_ms"), int) and detail["down_ms"] > 0 and isinstance(detail.get("dropped"), int),
                  f"UPSTREAM_RESTORED の detail に down_ms・dropped がある（{json.dumps(detail)}）")
            if fault is not None:
                check(lost.message["ts"] < fault.message["ts"] < rest.message["ts"],
                      "ts は発生時刻（LOST < SENSOR_FAULT < RESTORED）")
                print(f"  LOST ts={lost.message['ts']} / SENSOR_FAULT ts={fault.message['ts']} uptime_ms="
                      f"{fault.message['uptime_ms']} / RESTORED ts={rest.message['ts']}")
            ev_seqs = [r.message["seq"] for r in after if r.kind == "event"]
            check(ev_seqs == sorted(ev_seqs) and len(set(ev_seqs)) == len(ev_seqs), "event の seq は送った順に増える")
        except ValueError:
            check(False, "再接続後に state（online）・UPSTREAM_LOST・UPSTREAM_RESTORED がそろって届く")

        # 5. SIGKILL で止めて LWT を確かめる
        last_online = max((r for r in obs.snapshot() if mine(r) and r.kind == "state" and status(r) == "online"),
                          key=lambda r: r.message["seq"])
        n = len(obs.snapshot())
        dev.send_signal(signal.SIGKILL)
        dev.wait(10)
        lwt = wait_for(
            lambda: next((r for r in obs.snapshot()[n:] if mine(r) and r.kind == "state" and status(r) == "offline"), None),
            30,
        )
        if check(lwt is not None, "SIGKILL の後に LWT の offline の state が届く"):
            # LWT は接続時に登録し、登録時点の値（次に使う seq）を入れる（共通仕様 第5章）。
            # 再接続の直後に送る online の state がその値を使うので、mode の切り替えがなければ同じ値になる
            check(lwt.message["seq"] == last_online.message["seq"],
                  f"LWT の seq は再接続時に登録した値（最後の online と同じ {last_online.message['seq']}）で、seq を進めない")
        # retain を確かめる：つなぎ直して受け直す
        obs.stop()
        retained = Observer(host, port, password, args.site)
        retained.start()
        r = wait_for(lambda: next((x for x in retained.snapshot() if x.kind == "state" and x.retain), None), 5)
        retained.stop()
        check(r is not None and status(r) == "offline" and r.message.get("boot_id") == boot_id,
              "state の retain が同じ boot_id の offline になっている")
    finally:
        if dev.poll() is None:
            dev.kill()
            dev.wait(10)
        try:
            compose("start", "mosquitto")
        except subprocess.CalledProcessError:
            pass

    if check.failures:
        print(f"不合格 {len(check.failures)} 件")
        return 1
    print("すべて合格")
    return 0


if __name__ == "__main__":
    sys.exit(main())
