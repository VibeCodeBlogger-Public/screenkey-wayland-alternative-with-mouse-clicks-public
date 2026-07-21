#!/usr/bin/env node
// VENDORED from the brand landing-nav source (managed centrally) — do not hand-edit.
// Canonical copy: brand/landing-nav/guards/nav-check.mjs in the screenkey kitchen repo.
// Self-guard for a landing's nav header: it must come from the generator (nav:generated markers)
// and keep the badge order GitHub → 👁 views → ★ star (star rightmost). No dependencies.
//   node .github/nav-check.mjs <landing.html>
import { readFileSync } from 'node:fs';

// Pure check → [{name, ok}]. Exported so the kitchen selftest can dogfood it without a subprocess.
export function checkHtml(html) {
  const out = [];
  const push = (name, ok) => out.push({ name, ok: !!ok });

  const m = html.match(/<!-- nav:generated [\s\S]*?<!-- \/nav:generated -->/);
  push('nav:generated markers present (came from the generator, not a hand-write)', m);
  const region = m ? m[0] : '';

  const iGh = region.indexOf('btn-primary');
  const iViews = region.indexOf('id="viewsWrap"');
  const iStar = region.indexOf('id="ghStar"');
  const one =
    iGh !== -1 && iViews !== -1 && iStar !== -1 &&
    region.lastIndexOf('id="viewsWrap"') === iViews &&
    region.lastIndexOf('id="ghStar"') === iStar;
  push('exactly one GitHub / views / star pill in the nav', one);
  push('order GitHub → 👁 views → ★ star (star rightmost)', one && iGh < iViews && iViews < iStar);

  return out;
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const file = process.argv[2];
  if (!file) { console.error('usage: node nav-check.mjs <landing.html>'); process.exit(2); }
  const results = checkHtml(readFileSync(file, 'utf8'));
  let fail = 0;
  for (const r of results) { console.log((r.ok ? 'ok   ' : 'FAIL ') + r.name); if (!r.ok) fail++; }
  console.log(fail
    ? `\n✗ nav-check: ${fail} failed — regenerate the header from the brand landing-nav source; do not hand-edit.`
    : '\n✓ nav-check: landing nav intact (generated + order GitHub → 👁 → ★).');
  process.exit(fail ? 1 : 0);
}
