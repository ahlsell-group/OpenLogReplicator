"""CLI. See README.md.

  python -m olrt run --image IMG [--image IMG2] [--profile debezium,json] [SCENARIO...]
  python -m olrt record [SCENARIO...]          # phase 1 only
  python -m olrt replicate --image IMG ...     # phase 2 over an existing recording
  python -m olrt check --image IMG ...         # phase 3 + report
  python -m olrt db-up | db-down [--volume] | list
"""
import argparse
import concurrent.futures as cf
import datetime as dt
import json
import os
import pathlib
import sys
import traceback

from . import checks, connect, db, olr, olr_net, record, report, scenario
from . import settings as S


def run_dir(run_id):
    d = S.RESULTS / run_id
    d.mkdir(parents=True, exist_ok=True)
    latest = S.RESULTS / "latest"
    try:
        if latest.is_symlink() or latest.exists():
            latest.unlink()
        latest.symlink_to(run_id)
    except OSError:
        pass
    return d


def resolve_run(args):
    if args.run:
        return S.RESULTS / args.run
    return (S.RESULTS / "latest").resolve()


def cmd_record(args, rd):
    db.up()
    scs = scenario.discover(args.scenarios)
    if not scs:
        sys.exit("no scenarios matched")
    failed = []
    for sc in scs:
        if sc.mode != "file":
            continue  # recorded live per image in the replicate (or connect) phase
        try:
            record.record(sc, rd / "record" / sc.safe_id, token=rd.name)
        except Exception as e:  # keep going: one broken scenario must not hide the others
            failed.append(sc.id)
            (rd / "record" / sc.safe_id).mkdir(parents=True, exist_ok=True)
            (rd / "record" / sc.safe_id / "error.txt").write_text(traceback.format_exc())
            print(f"[record] {sc.id}: FAILED: {e}", flush=True)
    return failed


def load_recordings(rd, scs):
    out = {}
    for sc in scs:
        p = rd / "record" / sc.safe_id / "recording.json"
        if p.exists():
            out[sc.id] = json.loads(p.read_text())
    return out


def cmd_replicate(args, rd):
    scs = scenario.discover(args.scenarios)
    recs = load_recordings(rd, scs)
    jobs, net_jobs = [], []
    for img in args.image:
        info = olr.image_info(img)
        if info is None:
            sys.exit(f"image not found locally: {img} (docker pull / build it first)")
        (rd / "olr" / olr.slug(img)).mkdir(parents=True, exist_ok=True)
        (rd / "olr" / olr.slug(img) / "image.json").write_text(json.dumps({"image": img, **info}, indent=1))
        for prof in args.profile:
            for sc in scs:
                if sc.mode == "network":
                    net_jobs.append((img, prof, sc))
                elif sc.id in recs:
                    jobs.append((img, prof, sc))
    print(f"[olr] {len(jobs)} runs, {S.OLR_PARALLEL} in parallel", flush=True)

    def one(job):
        img, prof, sc = job
        w = rd / "olr" / olr.slug(img) / prof / sc.safe_id
        res = olr.run(img, recs[sc.id], sc, prof, w)
        print(f"[olr] {img} {prof} {sc.id}: exit={res['exit_code']} {res['seconds']}s"
              f"{' TIMEOUT' if res['timed_out'] else ''}", flush=True)

    with cf.ThreadPoolExecutor(S.OLR_PARALLEL) as ex:
        list(ex.map(one, jobs))
    # network mode runs the workload live against OLR: one at a time
    for img, prof, sc in net_jobs:
        w = rd / "olr" / olr.slug(img) / prof / sc.safe_id
        try:
            res = olr_net.run(img, sc, prof, w, token=rd.name)
            print(f"[olr-net] {img} {prof} {sc.id}: exit={res['exit_code']} {res['seconds']}s"
                  f"{' SKIP: ' + res['skip'] if res.get('skip') else ''}", flush=True)
        except Exception:
            w.mkdir(parents=True, exist_ok=True)
            (w / "error.txt").write_text(traceback.format_exc())
            print(f"[olr-net] {img} {prof} {sc.id}: FAILED, see {w / 'error.txt'}", flush=True)


def cmd_check(args, rd):
    scs = scenario.discover(args.scenarios)
    recs = load_recordings(rd, scs)
    results = {}
    for img in args.image:
        for prof in args.profile:
            for sc in scs:
                if sc.mode == "connect":
                    continue  # python -m olrt connect
                w = rd / "olr" / olr.slug(img) / prof / sc.safe_id
                if sc.mode == "network":
                    if not w.exists():
                        continue  # this image/profile was not replicated
                    run_p = w / "run.json"
                    ri = json.loads(run_p.read_text()) if run_p.exists() else None
                    if ri and ri.get("skip"):
                        results[(sc.id, img, prof)] = {c: {"status": "skip", "details": [ri["skip"]]}
                                                       for c in report.CHECKS}
                        continue
                    if not (w / "recording.json").exists():
                        err = w / "error.txt"
                        msg = err.read_text().strip().splitlines()[-1] if err.exists() else "no recording"
                        results[(sc.id, img, prof)] = {c: {"status": "error", "details": [msg]}
                                                       for c in report.CHECKS}
                        continue
                    recs_local = json.loads((w / "recording.json").read_text())
                else:
                    recs_local = recs.get(sc.id)
                if recs_local is None:
                    err = (rd / "record" / sc.safe_id / "error.txt")
                    msg = err.read_text().strip().splitlines()[-1] if err.exists() else "not recorded"
                    results[(sc.id, img, prof)] = {c: {"status": "error", "details": [f"recording failed: {msg}"]}
                                                   for c in report.CHECKS}
                    continue
                if not (w / "run.json").exists():
                    continue
                rec = recs_local
                run_info = json.loads((w / "run.json").read_text())
                out = checks.read_olr(w / "output.jsonl", rec)
                res = {"run": checks.check_run(sc, w, run_info, out, image=img)}
                if "diff" in sc.checks:
                    res["diff"] = checks.check_diff(sc, rec, out, prof)
                if "replay" in sc.checks:
                    res["replay"] = checks.check_replay(sc, rec, out)
                if "gap" in sc.checks:
                    res["gap"] = checks.check_gap(sc, rec, out, w, run_info)
                checks.apply_known(sc, prof, res, image=img)
                results[(sc.id, img, prof)] = {k: v.to_dict() for k, v in res.items()}
                (w / "checks.json").write_text(json.dumps(results[(sc.id, img, prof)], indent=1,
                                                          ensure_ascii=False))
    cols = report.CHECKS + (["gap"] if any("gap" in r for r in results.values()) else [])
    md = report.save(results, rd, args.image, args.profile, checks=cols)
    print(md)
    bad = [k for k, r in results.items() if any(v["status"] in ("fail", "error") for v in r.values())]
    return 1 if bad else 0


def cmd_connect(args, rd):
    """Real Debezium Connect: every image x profile x Debezium version x connect scenario."""
    scs = [sc for sc in scenario.discover(args.scenarios) if sc.mode == "connect"]
    if not scs:
        sys.exit("no connect scenarios matched")
    db.up()
    connect.ensure_dbz_user()
    connect.broker_up()
    results = {}
    cols = []
    for ver in args.dbz:
        connect.connect_up(ver)
        for img in args.image:
            for prof in args.profile:
                col = f"{prof}@dbz-{ver}"
                if col not in cols:
                    cols.append(col)
                for sc in scs:
                    w = rd / "connect" / olr.slug(img) / col / sc.safe_id
                    try:
                        ri = connect.run(img, sc, prof, ver, w, token=rd.name)
                        res = connect.check(sc, w)
                        print(f"[connect] dbz {ver} {img} {prof} {sc.id}: {ri['seconds']}s "
                              + " ".join(f"{k}={v['status']}" for k, v in res.items()), flush=True)
                    except Exception:
                        w.mkdir(parents=True, exist_ok=True)
                        (w / "error.txt").write_text(traceback.format_exc())
                        print(f"[connect] dbz {ver} {img} {prof} {sc.id}: ERROR, see {w / 'error.txt'}", flush=True)
                        res = {c: {"status": "error", "details": [traceback.format_exc().strip().splitlines()[-1]]}
                               for c in connect.CHECKS}
                    results[(sc.id, img, col)] = res
    md = report.save(results, rd, args.image, cols, checks=connect.CHECKS, name="connect")
    print(md)
    bad = [k for k, r in results.items() if any(v["status"] in ("fail", "error") for v in r.values())]
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(prog="olrt")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("run", "record", "replicate", "check"):
        p = sub.add_parser(name)
        p.add_argument("scenarios", nargs="*", help="scenario ids, groups (wide/), globs or tags")
        p.add_argument("--run", help="run id (directory under results/); default: new for run/record, latest otherwise")
        if name != "record":
            p.add_argument("--image", action="append", required=True)
            p.add_argument("--profile", default="debezium,json")
    p = sub.add_parser("connect", help="real Debezium Connect runs (scenarios with connect.toml)")
    p.add_argument("scenarios", nargs="*")
    p.add_argument("--run")
    p.add_argument("--image", action="append", required=True)
    p.add_argument("--profile", default="debezium")
    p.add_argument("--dbz", default=",".join(S.DEBEZIUM_VERSIONS),
                   help="Debezium versions, comma separated (image olrt/connect:<version>)")
    sub.add_parser("connect-down", help="remove the broker and Connect containers")
    p = sub.add_parser("connect-check", help="re-run the checks of a connect run (no containers)")
    p.add_argument("--run", required=True)
    sub.add_parser("db-up")
    p = sub.add_parser("db-down")
    p.add_argument("--volume", action="store_true", help="also remove the olrt-oradata volume")
    sub.add_parser("list")
    args = ap.parse_args()
    if hasattr(args, "profile") and isinstance(args.profile, str):
        args.profile = [x for x in args.profile.split(",") if x]
        for x in args.profile:
            if x not in olr.PROFILES:
                sys.exit(f"unknown profile {x}; known: {', '.join(olr.PROFILES)}")

    if args.cmd == "db-up":
        db.up()
        print("olrt-oracle ready on 127.0.0.1:%d" % S.ORACLE_PORT)
        return 0
    if args.cmd == "db-down":
        db.down(volume=args.volume)
        return 0
    if args.cmd == "connect-check":
        rd = S.RESULTS / args.run
        scs = {sc.safe_id: sc for sc in scenario.discover() if sc.mode == "connect"}
        results, imgs, cols = {}, [], []
        for w in sorted((rd / "connect").glob("*/*/*")):
            sc = scs.get(w.name)
            if sc is None or not (w / "recording.json").exists():
                continue
            img = json.loads((w / "run.json").read_text())["image"]
            col = w.parent.name
            imgs += [img] if img not in imgs else []
            cols += [col] if col not in cols else []
            results[(sc.id, img, col)] = connect.check(sc, w)
        print(report.save(results, rd, imgs, cols, checks=connect.CHECKS, name="connect"))
        return 0
    if args.cmd == "connect-down":
        connect.down()
        return 0
    if args.cmd == "connect":
        args.dbz = [x for x in args.dbz.split(",") if x]
        rd = run_dir(args.run or "connect-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
        print(f"results: {rd}", flush=True)
        return cmd_connect(args, rd)
    if args.cmd == "list":
        for sc in scenario.discover():
            print(f"{sc.id:45s} {sc.mode:8s} {sc.description}")
        return 0

    if args.cmd in ("run", "record"):
        rid = args.run or dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        rd = run_dir(rid)
    else:
        rd = resolve_run(args)
    print(f"results: {rd}", flush=True)
    if args.cmd == "record":
        return 1 if cmd_record(args, rd) else 0
    if args.cmd == "replicate":
        cmd_replicate(args, rd)
        return 0
    if args.cmd == "check":
        return cmd_check(args, rd)
    # run
    if not (rd / "record").exists() or args.run is None:
        cmd_record(args, rd)
    cmd_replicate(args, rd)
    return cmd_check(args, rd)


if __name__ == "__main__":
    os.umask(0o002)
    sys.exit(main())
