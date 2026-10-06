"""Summary table (Markdown) and JUnit XML."""
import json
import xml.etree.ElementTree as ET

CHECKS = ["run", "diff", "replay"]
SYMBOL = {"pass": "PASS", "fail": "FAIL", "xfail": "xfail", "xpass": "XPASS", "skip": "-", "error": "ERR"}


def summary_md(results, images, profiles, checks=CHECKS):
    """results: {(scenario, image, profile): {check: Result-dict}}"""
    scenarios = sorted({k[0] for k in results})
    cols = [(i, p) for i in images for p in profiles]
    lines = []
    lines.append(f"Cells are {' / '.join(checks)}. PASS, FAIL, xfail (known issue, still failing), "
                 "XPASS (known issue no longer reproduces), - (not run).\n")
    for n, (i, p) in enumerate(cols, start=1):
        lines.append(f"- [{n}] `{i}` profile `{p}`")
    lines.append("")
    lines.append("| scenario | " + " | ".join(f"[{n}]" for n in range(1, len(cols) + 1)) + " |")
    lines.append("|---|" + "---|" * len(cols))
    for s in scenarios:
        cells = []
        for i, p in cols:
            r = results.get((s, i, p))
            if not r:
                cells.append("-")
                continue
            cells.append(" / ".join(SYMBOL[r[c]["status"]] if c in r else "-" for c in checks))
        lines.append(f"| {s} | " + " | ".join(cells) + " |")
    totals = []
    for i, p in cols:
        rs = [results[k] for k in results if k[1] == i and k[2] == p]
        bad = sum(1 for r in rs if any(r[c]["status"] in ("fail", "error") for c in r))
        totals.append(f"`{i}` `{p}`: {len(rs) - bad}/{len(rs)} scenarios without unexpected failures")
    lines.append("")
    lines += [f"- {t}" for t in totals]
    return "\n".join(lines) + "\n"


def details_md(results):
    out = []
    for (s, i, p), r in sorted(results.items()):
        bad = {c: v for c, v in r.items() if v["status"] not in ("pass", "skip")}
        if not bad:
            continue
        out.append(f"### {s} — `{i}` `{p}`\n")
        for c, v in bad.items():
            out.append(f"- **{c}: {v['status']}**")
            for d in v["details"]:
                out.append(f"  - {d}")
        out.append("")
    return "\n".join(out) + "\n"


def junit(results, path):
    suites = ET.Element("testsuites")
    by_suite = {}
    for (s, i, p), r in sorted(results.items()):
        by_suite.setdefault((i, p), []).append((s, r))
    for (i, p), items in by_suite.items():
        suite = ET.SubElement(suites, "testsuite", name=f"{i} [{p}]")
        n = f = sk = 0
        for s, r in items:
            for c, v in r.items():
                n += 1
                tc = ET.SubElement(suite, "testcase", classname=f"{s}", name=f"{c} [{p}] {i}")
                text = "\n".join(v["details"])
                if v["status"] in ("fail", "error"):
                    f += 1
                    el = ET.SubElement(tc, "failure", message=(v["details"] or [v["status"]])[0][:500])
                    el.text = text
                elif v["status"] in ("xfail", "skip"):
                    sk += 1
                    ET.SubElement(tc, "skipped", message=(v["details"] or [v["status"]])[0][:500])
                elif text:
                    ET.SubElement(tc, "system-out").text = text
        suite.set("tests", str(n))
        suite.set("failures", str(f))
        suite.set("skipped", str(sk))
    ET.ElementTree(suites).write(path, encoding="utf-8", xml_declaration=True)


def save(results, outdir, images, profiles, checks=CHECKS, name="results"):
    flat = {f"{s}|{i}|{p}": r for (s, i, p), r in results.items()}
    (outdir / f"{name}.json").write_text(json.dumps(flat, indent=1, ensure_ascii=False))
    md = "# OLR differential test results\n\n" + summary_md(results, images, profiles, checks) + \
         "\n## Details\n\n" + details_md(results)
    (outdir / ("summary.md" if name == "results" else f"{name}-summary.md")).write_text(md)
    junit(results, outdir / ("junit.xml" if name == "results" else f"{name}-junit.xml"))
    return md
