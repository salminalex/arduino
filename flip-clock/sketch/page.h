/*
 * Static parts of the settings page, served as /style.css and /app.js so the
 * browser caches them instead of refetching on every load. Kept out of
 * network.ino so they read as CSS and script, not as glued together C
 * string literals.
 */

#pragma once

static const char PAGE_CSS[] = R"CSS(body{font:16px system-ui;margin:0;padding:24px;background:#111;color:#eee}
h1{font-size:20px;margin:0 0 20px}
label{display:block;margin:14px 0 4px;color:#aaa;font-size:14px}
input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;
  border:1px solid #444;background:#1c1c1c;color:#eee;font-size:16px}
button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:8px;
  background:#e0e0e0;color:#111;font-size:16px;font-weight:600}
summary{color:#7ab7ff;cursor:pointer;font-size:14px;margin:10px 0}
.row{display:flex;gap:10px;margin-top:16px}
.row form{flex:1}
.row button{margin:0;background:#2a2a2a;color:#ccc;font-weight:400}
.tzrow{display:flex;gap:10px}
.tzrow select:first-child{flex:0 0 38%}
.tzrow select:last-child{flex:1;min-width:0}
.chk{display:flex;align-items:center;gap:8px;margin-top:10px}
.chk input{width:auto}
.n{color:#777;font-size:13px;margin-top:16px}
)CSS";

/*
 * The timezone is derived from the browser itself: it knows its own offset and
 * can be asked what that offset was on any date, which is enough to recover the
 * transition dates and build a POSIX TZ string. No list, no lookup, and it works
 * in the portal where there is no internet.
 *
 * The region/city picker below is only for gifting the clock into another zone.
 * It needs the IANA table, so it appears only when the network allows.
 */
static const char PAGE_JS[] = R"JS(const s    = document.getElementById('tz'),
      pick = document.getElementById('tzpick'),
      reg  = document.getElementById('tzregion'),
      city = document.getElementById('tzcity'),
      val  = document.getElementById('tzval'),
      info = document.getElementById('tzinfo'),
      man  = document.getElementById('tzman');

const CUR  = s.value;
const MINE = Intl.DateTimeFormat().resolvedOptions().timeZone;

// minutes east of UTC at an instant; POSIX counts the other way round
const off = t => -new Date(t).getTimezoneOffset();

const pf = e => {
  const p = -e, a = Math.abs(p), m = a % 60;
  return (p < 0 ? '-' : '') + Math.floor(a / 60) +
         (m ? ':' + String(m).padStart(2, '0') : '');
};

// binary search for the instant the offset changes
const edge = (lo, hi) => {
  while (hi - lo > 60000) {
    const m = Math.floor((lo + hi) / 2);
    if (off(m) === off(lo)) lo = m; else hi = m;
  }
  return hi;
};

// that instant as Mmonth.week.weekday/hour in local wall clock time
const rule = t => {
  const d   = new Date(t + off(t - 60000) * 60000);
  const dim = new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth() + 1, 0)).getUTCDate();
  const day = d.getUTCDate();
  const w   = day + 7 > dim ? 5 : Math.ceil(day / 7);
  // 2:00 is the POSIX default; anything else has to be spelled out, and a few
  // zones switch on a quarter hour
  const h = d.getUTCHours(), mi = d.getUTCMinutes();
  const at = (h === 2 && !mi) ? '' : '/' + h + (mi ? ':' + String(mi).padStart(2, '0') : '');
  return 'M' + (d.getUTCMonth() + 1) + '.' + w + '.' + d.getUTCDay() + at;
};

const detect = () => {
  const Y = new Date().getFullYear();
  const jan = off(Date.UTC(Y, 0, 1)), jul = off(Date.UTC(Y, 6, 1));
  if (jan === jul) return 'STD' + pf(jan);

  const a = edge(Date.UTC(Y, 0, 1), Date.UTC(Y, 6, 1)),
        b = edge(Date.UTC(Y, 6, 1), Date.UTC(Y + 1, 0, 1)),
        d = Math.max(jan, jul);
  const st = off(a + 60000) === d ? a : b;
  return 'STD' + pf(Math.min(jan, jul)) + 'DST' + pf(d) + ',' + rule(st) + ',' + rule(st === a ? b : a);
};

const clock = z => {
  try {
    return new Intl.DateTimeFormat([], Object.assign(
      {hour: '2-digit', minute: '2-digit', hour12: false}, z ? {timeZone: z} : {})).format();
  } catch (e) { return ''; }
};

let AUTO = '';
try { AUTO = detect(); } catch (e) {}

const useAuto = () => {
  val.value = AUTO;
  info.textContent = 'This device: ' + MINE.replace(/_/g, ' ') + ', ' + clock() + '  (' + AUTO + ')';
};

const useManual = v => {
  val.value = v;
  const z = reg.value && city.value
          ? reg.value + '/' + ((city.selectedOptions[0] || {}).text || '').replace(/ /g, '_')
          : '';
  const t = clock(z);
  info.textContent = 'Set manually: ' + (z ? z.replace(/_/g, ' ') : v) + (t ? ', ' + t : '');
};

s.removeAttribute('name');
val.name = 'tz';
s.onchange = () => useManual(s.value);

if (AUTO && (!CUR || CUR === 'UTC0')) useAuto();
else { useManual(CUR); man.open = true; }

let T = {};

const cities = (r, name) => {
  city.innerHTML = '';
  for (const [n, v] of T[r]) city.add(new Option(n, v));
  // many zones share one POSIX string, so select by name, not by value
  if (name) for (const o of city.options) if (o.text === name) { o.selected = true; break; }
  if (city.selectedIndex < 0) city.selectedIndex = 0;
  useManual(city.value);
};

reg.onchange  = () => cities(reg.value);
city.onchange = () => useManual(city.value);

fetch('https://cdn.jsdelivr.net/gh/nayarsystems/posix_tz_db@master/zones.json')
  .then(r => r.json())
  .then(z => {
    for (const k in z) {
      const i = k.indexOf('/');
      const r = i < 0 ? 'Other' : k.slice(0, i), c = i < 0 ? k : k.slice(i + 1);
      (T[r] = T[r] || []).push([c.replace(/_/g, ' '), z[k]]);
    }
    for (const g of Object.keys(T).sort()) reg.add(new Option(g, g));

    const want = val.value;
    let name = null;
    for (const k in z) if (z[k] === want) { name = k; break; }
    if (!name) name = z[MINE] ? MINE : 'UTC';

    const i = name.indexOf('/');
    reg.value = i < 0 ? 'Other' : name.slice(0, i);
    city.innerHTML = '';
    for (const [n, v] of T[reg.value]) city.add(new Option(n, v));

    const w = (i < 0 ? name : name.slice(i + 1)).replace(/_/g, ' ');
    for (const o of city.options) if (o.text === w) { o.selected = true; break; }

    s.hidden = true;
    pick.hidden = false;
  })
  .catch(() => {});
)JS";

static const char PAGE_JS_FORMS[] = R"JS(
// Submit in place instead of navigating away. The board answers with what it
// did: "ok" when the change was applied live, "restart" when the radio has to
// come back up, "reload" when the page itself holds stale data.
for (const f of document.querySelectorAll('form[data-ajax]')) {
  f.onsubmit = e => {
    e.preventDefault();
    const b = f.querySelector('button'), label = b.textContent;
    b.textContent = 'working...';

    fetch(f.action, {method: 'POST', body: new URLSearchParams(new FormData(f))})
      .then(r => r.ok ? r.text() : Promise.reject())
      .then(t => {
        if (t === 'reload') { location.reload(); return; }
        b.textContent = t === 'restart' ? 'restarting...' : 'saved';
        setTimeout(() => b.textContent = label, 2000);
      })
      .catch(() => {
        b.textContent = 'failed';
        setTimeout(() => b.textContent = label, 2000);
      });
  };
}
)JS";
