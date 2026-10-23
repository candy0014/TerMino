#!/usr/bin/env python3
"""Reproduce a stratified QP2 public-score sample and its pressure statistics.

Only the documented Tetra Channel API is used. Full replay events are not
included in this endpoint: a run's average is not a per-floor incoming rate.
"""

import argparse
import json
import hashlib
import math
import sys
import statistics
import time
import urllib.parse
import urllib.request
from pathlib import Path

ANCHORS = [25, 100, 225, 375, 550, 750, 975, 1225, 1500, 2000, 3000, 4000, 5000]
FIT_ANCHORS = ANCHORS[:-1] + [6000]


def download(directory, weeks, pages=1, anchors=ANCHORS):
    directory.mkdir(parents=True, exist_ok=True)
    for week in weeks:
        for anchor in anchors:
            cursor = f"{anchor}:0:0"
            for page in range(pages):
                path = directory / f"{week}_{anchor}_{page}.json"
                if path.exists():
                    data = json.loads(path.read_text(encoding="utf-8"))
                else:
                    query = urllib.parse.urlencode({"limit": 100, "after": cursor})
                    url = f"https://ch.tetr.io/api/records/zenith_global@{week}?{query}"
                    request = urllib.request.Request(
                        url,
                        headers={
                            "User-Agent": "TerMino-QP2-pressure-study/2.0",
                            "X-Session-ID": "termino-qp2-pressure-20261020",
                        },
                    )
                    for attempt in range(4):
                        try:
                            with urllib.request.urlopen(request, timeout=30) as response:
                                data = json.load(response)
                            if not data.get("success"):
                                raise RuntimeError(data.get("error"))
                            break
                        except (OSError, RuntimeError):
                            if attempt == 3:
                                raise
                            time.sleep(2 + attempt * 3)
                    path.write_text(json.dumps(data), encoding="utf-8")
                    time.sleep(1)  # Documented public API rate; reuse all caches.
                entries = data["data"]["entries"]
                print(f"{week} <{anchor}m page {page}: {len(entries)} records", flush=True)
                if len(entries) < 100:
                    break
                last = entries[-1]["p"]
                cursor = ":".join(str(last[k]) for k in ("pri", "sec", "ter"))


def percentile(values, p):
    ordered = sorted(values)
    x = (len(ordered) - 1) * p
    lo = math.floor(x)
    hi = math.ceil(x)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (x - lo)


def summarize(directory):
    records = {}
    for path in sorted(directory.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        for record in data.get("data", {}).get("entries", []):
            records[record["_id"]] = record
    buckets = {}
    mix_bands = [[0] * 5 for _ in range(6)]
    unmodded_total = 0
    for record in records.values():
        if record["extras"]["zenith"]["mods"]:
            continue
        unmodded_total += 1
        stats = record["results"]["stats"]
        minutes = stats["finaltime"] / 60000
        if minutes <= 0:
            continue
        altitude = stats["zenith"]["altitude"]
        band = (
            0
            if altitude < 150
            else (
                1
                if altitude < 450
                else 2 if altitude < 850 else 3 if altitude < 1650 else 4 if altitude < 3500 else 5
            )
        )
        clears = stats["clears"]
        counts = [
            clears["singles"] + clears["doubles"],
            clears["triples"] + clears["minitspinsingles"],
            clears["tspinsingles"] + clears["minitspindoubles"],
            clears["quads"] + clears["tspindoubles"],
            clears["tspintriples"],
        ]
        mix_bands[band] = [a + b for a, b in zip(mix_bands[band], counts)]
        anchor = min(ANCHORS, key=lambda x: abs(x - altitude))
        # The upper-bound query can run below its intended band in sparse tails.
        if abs(anchor - altitude) > max(75, anchor * 0.15):
            continue
        buckets.setdefault(anchor, []).append(
            {
                "altitude": altitude,
                "minutes": minutes,
                "apm": record["results"]["aggregatestats"]["apm"],
                "sent": stats["garbage"]["sent"] / minutes,
                "received": stats["garbage"]["received"] / minutes,
            }
        )
    summary = {
        "source": "https://ch.tetr.io/api/records/:leaderboard",
        "sampled_records": len(records),
        "unmodded_total": unmodded_total,
        "unmodded_records": sum(map(len, buckets.values())),
        "sampling": "100 PBs immediately below each altitude anchor, two completed weekly leaderboards; not a population-random sample",
        "metric": "whole-run outgoing and received rows/minute; incoming includes official reception adjustments and Windup clipping",
        "bands": [],
        "clear_mix": [
            {
                "sizes": [1, 2, 3, 5, 7],
                "counts": counts,
                "weights": [round(x / sum(counts), 4) for x in counts],
            }
            for counts in mix_bands
        ],
    }
    for anchor, rows in sorted(buckets.items()):

        def describe(field):
            values = [row[field] for row in rows]
            return {
                "mean": statistics.mean(values),
                "median": statistics.median(values),
                "p10": percentile(values, 0.1),
                "p90": percentile(values, 0.9),
            }

        summary["bands"].append(
            {
                "anchor": anchor,
                "n": len(rows),
                "altitude_mean": statistics.mean(row["altitude"] for row in rows),
                "apm": describe("apm"),
                "sent": describe("sent"),
                "received": describe("received"),
            }
        )
    return summary


def fit_pressure(directory, progress=False):
    """Fit conditional mean and variance from floor-exposure integrals.

    Requires NumPy only with --fit. Latent lognormal intensity is shared
    across a run. Its covariance is integrated using Gauss-Hermite nodes,
    matching the generator, rather than fitting a single constant variance.
    """
    import numpy as np

    records = {}
    for p in sorted(directory.glob("*.json")):
        for r in json.loads(p.read_text()).get("data", {}).get("entries", []):
            records[r["_id"]] = r
    rows = []
    floorheights = [50, 150, 300, 450, 650, 850, 1100, 1350, 1650]
    for r in records.values():
        if r["extras"]["zenith"]["mods"]:
            continue
        s = r["results"]["stats"]
        h = s["zenith"]["altitude"]
        a = r["results"]["aggregatestats"]["apm"]
        T = s["finaltime"] / 60000
        if T < 0.25 or not (0 <= h <= 6000 and 0 <= a <= 500):
            continue
        times = [0]
        heights = [0]
        for height, t in zip(floorheights, s["zenith"]["splits"]):
            if height <= h and t > times[-1] * 60000 and t < s["finaltime"]:
                times.append(t / 60000)
                heights.append(height)
        times.append(T)
        heights.append(h)
        # Four midpoint quadrature samples per observed floor interval.
        H = []
        D = []
        fatigue = []
        for i in range(len(times) - 1):
            for q in range(4):
                H.append(heights[i] + (heights[i + 1] - heights[i]) * (q + 0.5) / 4)
                D.append((times[i + 1] - times[i]) / 4)
                t = (times[i] + (times[i + 1] - times[i]) * (q + 0.5) / 4) * 60
                fatigue.append(1 + 0.25 * (t >= 540) + 0.25 * (t >= 660))
        rows.append(
            {
                "id": r["_id"],
                "user": hashlib.sha256(r["user"]["id"].encode()).hexdigest()[:16],
                "ts": r["ts"],
                "week": r["revolution"],
                "altitude": h,
                "apm": a,
                "minutes": T,
                "received": s["garbage"]["received"],
                "heights": H,
                "durations": D,
                "fatigue": fatigue,
            }
        )
    rows.sort(key=lambda r: r["id"])
    input_bytes = json.dumps(rows).encode()
    N = len(rows)
    Q = max(len(r["heights"]) for r in rows)
    X = np.zeros((N, Q, 6))
    D = np.zeros((N, Q))
    Y = np.array([r["received"] for r in rows])
    T = np.array([r["minutes"] for r in rows])

    def features(h, a):
        x = np.log1p(np.asarray(h) / 500)
        z = np.ones_like(x) * np.log1p(np.asarray(a) / 40)
        return np.array([np.ones_like(x), x, z, x * z, x * x, z * z]).T

    for i, r in enumerate(rows):
        n = len(r["heights"])
        X[i, :n] = features(r["heights"], r["apm"])
        D[i, :n] = np.array(r["durations"]) * r["fatigue"]
    train = np.array([int(r["user"][:8], 16) % 5 != 0 for r in rows])
    test = ~train
    b = np.array([2.12, 0.67, 1.81, 2.47, -1.36, -1.83])
    ridge = np.diag([0.00001, 0.01, 0.01, 0.1, 0.1, 0.1])

    def project(beta):
        beta = beta.copy()
        beta[1] = max(0.0, beta[1])
        beta[3] = max(0.0, beta[3])
        beta[4] = min(-1e-6, beta[4])
        return beta

    def prediction(beta):
        # Saturate the altitude response at its fitted peak. Whole-run totals
        # cannot justify a difficulty curve that turns down above that peak.
        effective = X.copy()
        h = np.minimum(
            X[:, :, 1], np.maximum(0.0, -(beta[1] + beta[3] * X[:, :, 2]) / (2 * beta[4]))
        )
        effective[:, :, 1] = h
        effective[:, :, 3] = h * X[:, :, 2]
        effective[:, :, 4] = h * h
        raw = effective @ beta
        E = np.exp(np.clip(raw, 0, np.log(180))) * D
        mu = E.sum(1)
        J = np.einsum("nq,nqk->nk", E * ((raw > 0) & (raw < np.log(180))), effective)
        return mu, J, E

    for it in range(70):
        mu, J, _ = prediction(b)
        v = mu + mu * mu * 0.15
        W = train / np.maximum(v, 1)
        step = np.linalg.solve(J.T @ (W[:, None] * J) + ridge, J.T @ (W * (Y - mu)) - ridge @ b)

        def loss(beta):
            beta = project(beta)
            return np.sum(train * (Y - prediction(beta)[0]) ** 2 / v) + beta @ ridge @ beta

        amount = 1.0
        previous_loss = loss(b)
        while amount > 1e-6 and loss(b + step * amount) > previous_loss:
            amount *= 0.5
        next_b = project(b + step * amount)
        change = np.max(abs(next_b - b))
        b = next_b
        if change < 1e-8:
            break
    mu, J, E = prediction(b)
    res2 = (Y - mu) ** 2
    nodes, ghweights = np.polynomial.hermite.hermgauss(12)
    nodes *= np.sqrt(2)
    ghweights /= np.sqrt(np.pi)
    g = np.array([-6.0, 8.0, -4.0, -2.0, -2.0, 3.0, 0.0])
    vridge = np.diag([0.00001, 0.05, 0.05, 0.5, 0.5, 0.5, 0.001])

    def variance_prediction(gamma, derivative=False):
        logcv = X @ gamma[:6]
        cv = np.exp(np.clip(logcv, -9, 0))
        sigma = np.sqrt(np.log1p(cv))
        ds = cv / (2 * sigma * (1 + cv)) * ((logcv > -9) & (logcv < 0))
        second = np.zeros(N)
        grad = np.zeros((N, 6))
        for node, weight in zip(nodes, ghweights):
            draw = E * np.exp(sigma * node - 0.5 * sigma * sigma)
            intensity = draw.sum(1)
            second += weight * intensity**2
            if derivative:
                grad += (
                    2
                    * weight
                    * intensity[:, None]
                    * np.einsum("nq,nqk->nk", draw * (node - sigma) * ds, X)
                )
        shot = np.exp(gamma[6])
        V = np.maximum(shot * mu + second - mu * mu, 1e-8)
        if derivative:
            return V, np.column_stack([grad, shot * mu]) / V[:, None]
        return V

    for it in range(100):
        V, Jv = variance_prediction(g, True)
        if progress and it % 10 == 0:
            print(
                f"variance iteration {it}: standardized residual {float(np.mean(res2[train]/V[train])):.4f}",
                file=sys.stderr,
                flush=True,
            )
        step = np.linalg.solve(
            Jv.T @ (train[:, None] * Jv) + vridge, Jv.T @ (train * (res2 / V - 1)) - vridge @ g
        )

        def loss(gamma):
            V = variance_prediction(gamma)
            return np.sum(train * (np.log(V) + res2 / V)) + gamma @ vridge @ gamma

        amount = 1.0
        previous_loss = loss(g)
        while amount > 1e-6 and loss(g + step * amount) > previous_loss:
            amount *= 0.5
        g += step * amount
        if np.max(abs(step * amount)) < 1e-7 or abs(previous_loss - loss(g)) < 1e-8 * max(
            1.0, abs(previous_loss)
        ):
            break
    shot = float(np.exp(g[6]))
    V = variance_prediction(g)
    out = {
        "downloaded_records": len(records),
        "unmodded_fit_records": N,
        "unique_players": len(set(r["user"] for r in rows)),
        "first_ts": min(r["ts"] for r in rows),
        "last_ts": max(r["ts"] for r in rows),
        "train_n": int(train.sum()),
        "holdout_n": int(test.sum()),
        "mean_coefficients": b.tolist(),
        "cv2_coefficients": g[:6].tolist(),
        "shot_factor": shot,
        "mean_model": "integral fatigue(t) * exp([1,hlog,alog,hlog*alog,hlog^2,alog^2] beta) dt_minutes; alog=log1p(APM/40), hlog=min(log1p(h/500),max(0,-(beta1+beta3*alog)/(2*beta4)))",
        "variance_model": "shot_factor*expected_count + Var_Z(sum_i exposure_i * mean_i * exp(sigma_i*Z - sigma_i^2/2))); sigma_i^2=log1p(exp(clamp(features_i gamma,-9,0))); 12-node Gauss-Hermite quadrature",
        "mean_quadrature": "linear interpolation between reported floor splits; four midpoint samples per interval; fixed normal fatigue offset 1/1.25/1.5 at 0/540/660 seconds; APM is whole-run own APM",
        "sampling": "4 pages of up to 100 PBs below 13 altitude anchors per completed week, 2026w41/w42; deduplicated; no Mods; >=15 seconds",
        "holdout": "20% of players by stable SHA256 split; no player shared with training",
        "constraints": "nondecreasing altitude response at fixed own APM; log-quadratic altitude saturates at fitted peak; mean clamped 1..180 rows/minute",
        "inputs_sha256": hashlib.sha256(input_bytes).hexdigest(),
        "checks": {},
    }
    for label, mask in [("train", train), ("holdout", test)]:
        out["checks"][label] = {
            "observed_mean_received_per_minute": float(np.mean(Y[mask] / T[mask])),
            "predicted_mean_received_per_minute": float(np.mean(mu[mask] / T[mask])),
            "rmse_per_minute": float(np.mean(((Y - mu)[mask] / T[mask]) ** 2) ** 0.5),
            "standardized_residual_mean_square": float(np.mean(res2[mask] / V[mask])),
            "observed_rate_variance": float(np.var(Y[mask] / T[mask])),
            "predicted_rate_variance": float(
                np.mean(V[mask] / T[mask] ** 2) + np.var(mu[mask] / T[mask])
            ),
        }
    out["grid"] = []
    for h in [0, 100, 500, 1000, 2000, 4000, 6000]:
        for a in [0, 20, 40, 80, 160]:
            f = features(h, a)
            hm = min(f[1], max(0.0, -(b[1] + b[3] * f[2]) / (2 * b[4])))
            fm = features(500 * np.expm1(hm), a)
            m = float(np.exp(np.clip(fm @ b, 0, np.log(180))))
            cv = float(np.exp(np.clip(f @ g[:6], -9, 0)))
            out["grid"].append(
                {
                    "altitude": h,
                    "own_apm": a,
                    "mean_received_per_minute": m,
                    "cv2": cv,
                    "variance_one_minute": shot * m + cv * m * m,
                }
            )
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--weeks", nargs="+", default=None)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--pages", type=int, default=1, choices=range(1, 11))
    parser.add_argument(
        "--fit", action="store_true", help="fit received mean and variance (requires NumPy)"
    )
    args = parser.parse_args()
    if args.download:
        download(
            args.directory,
            args.weeks or (["2026w41", "2026w42"] if args.fit else ["2026w40", "2026w41"]),
            args.pages,
            FIT_ANCHORS if args.fit else ANCHORS,
        )
    summary = fit_pressure(args.directory, progress=True) if args.fit else summarize(args.directory)
    text = json.dumps(summary, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text)
