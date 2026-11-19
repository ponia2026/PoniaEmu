.pragma library
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Ponia theme: system data table, recognition, sorting, colours
// Sort order: home consoles -> arcade -> handhelds -> computers -> other; by release year within a category; unknown years last, then by name.
// A user's own collection that is not in the table: generic gamepad icon + the original collection name + category "other", no year shown.

var CAT_ORDER = { home: 0, arcade: 1, hand: 2, computer: 3, other: 4, multi: 4 };   // multi = old name, same as other (compatible with old overrides.json)

// k = icon file name; n = display name (system names are not translated); c = category; y = year; a = recognisable short names (non-alphanumerics removed, lower-cased)
var CATALOG = [
  { k: "fc", ac: "#E5484D",      n: "Family Computer",  c: "home",   y: 1983, a: ["fc", "nes", "famicom", "fds", "famicomdisksystem"] },
  { k: "pce", ac: "#F4A259",     n: "PC Engine",        c: "home",   y: 1987, a: ["pce", "pcengine", "tg16", "turbografx16", "turbografx", "pcenginecd", "tgcd", "supergrafx", "sgx"] },
  { k: "atari2600", ac: "#D49A6A", n: "Atari",         c: "home",   y: 1977, a: ["atari2600", "a2600", "atari", "vcs"] },
  { k: "sms", ac: "#4DB6AC",     n: "Master System",    c: "home",   y: 1985, a: ["sms", "mastersystem", "segamastersystem", "markiii"] },
  { k: "genesis", ac: "#5B8DEF", n: "Genesis/Mega Drive",          c: "home",   y: 1988, a: ["genesis", "megadrive", "md", "segagenesis", "segacd", "megacd", "sega32x", "32x"] },
  { k: "neogeo", ac: "#E6B84F",  n: "Neo Geo",          c: "home",   y: 1990, a: ["neogeo", "neogeocd", "ngcd", "aes", "mvs"] },
  { k: "sfc", ac: "#9B84E0",     n: "Super Famicom",    c: "home",   y: 1990, a: ["sfc", "snes", "superfamicom", "supernintendo", "satellaview"] },
  { k: "saturn", ac: "#E87CA0",  n: "Saturn",           c: "home",   y: 1994, a: ["saturn", "segasaturn", "ss"] },
  { k: "ps1", ac: "#59A9F0",     n: "PlayStation",      c: "home",   y: 1994, a: ["ps1", "psx", "ps", "psone", "playstation"] },
  { k: "n64", ac: "#4CC38A",     n: "Nintendo 64",      c: "home",   y: 1996, a: ["n64", "nintendo64"] },
  { k: "dc", ac: "#FF8A5B",      n: "Dreamcast",        c: "home",   y: 1998, a: ["dc", "dreamcast", "segadreamcast"] },
  { k: "gc", ac: "#8E7DEA",      n: "GameCube",         c: "home",   y: 2001, a: ["gc", "gamecube", "ngc", "nintendogamecube"] },
  { k: "fbneo", ac: "#C96BD8",   n: "Final Burn Neo",   c: "arcade", y: 1979, a: ["fbneo", "fba", "fbalpha", "finalburnneo", "finalburnalpha"] },
  { k: "cps1", ac: "#D9825B",    n: "CPS-1",            c: "arcade", y: 1988, a: ["cps1", "cps"] },
  { k: "cps2", ac: "#D9A25B",    n: "CPS-2",            c: "arcade", y: 1993, a: ["cps2", "cps3"] },
  { k: "mame", ac: "#4FC3E0",    n: "MAME",             c: "arcade", y: 1997, a: ["mame", "arcade", "mame2003", "mame2010", "mame2003plus"] },
  { k: "gb", ac: "#9CCB6B",      n: "Game Boy",         c: "hand",   y: 1989, a: ["gb", "gameboy"] },
  { k: "gbc", ac: "#F2B84B",     n: "Game Boy Color",   c: "hand",   y: 1998, a: ["gbc", "gameboycolor"] },
  { k: "gba", ac: "#6BA7F0",     n: "Game Boy Advance", c: "hand",   y: 2001, a: ["gba", "gameboyadvance"] },
  { k: "nds", ac: "#C9A0DC",     n: "Nintendo DS",      c: "hand",   y: 2004, a: ["nds", "ds", "nintendods"] },
  { k: "psp", ac: "#5E6AD2",     n: "PSP",              c: "hand",   y: 2004, a: ["psp", "playstationportable"] },
  { k: "apple2", ac: "#A9D86B",  n: "Apple II",         c: "computer",  y: 1977, a: ["apple2", "appleii", "apple2gs"] },
  { k: "dos", ac: "#3FA7C9",      n: "DOS",              c: "computer",  y: 1981, a: ["dos", "msdos"] },
  { k: "pico8", ac: "#FF6B9D",   n: "Pico-8",           c: "other",  y: 2015, a: ["pico8", "pico", "fantasyconsole"] },
  { k: "ports", ac: "#B58CE0",   n: "Ports",            c: "other",  y: 0,    a: ["ports", "port", "portmaster", "openbor", "native"] }
];

var _index = null;
function _buildIndex() {
  _index = {};
  for (var i = 0; i < CATALOG.length; i++)
    for (var j = 0; j < CATALOG[i].a.length; j++)
      _index[CATALOG[i].a[j]] = i;
}

function norm(s) {
  return (s || "").toLowerCase().replace(/[^a-z0-9]/g, "");
}

// Returns { idx, hack }; idx = -1 when not found. A modified version such as "xxx hack" borrows the original system.
function _lookup(str) {
  if (!_index) _buildIndex();
  var k = norm(str);
  if (_index[k] !== undefined) return { idx: _index[k], hack: false };
  var k2 = k.replace(/hacks?$/, "");
  if (k2 !== k && _index[k2] !== undefined) return { idx: _index[k2], hack: true };
  // prefix: such as "FBNEO ACT", "MAME ETC" (Chinese repacks often split by type into several collections). Only short names of length >= 4 count, the longest wins.
  var best = "", bi = -1;
  for (var a in _index) {
    if (a.length >= 4 && a.length > best.length && k.indexOf(a) === 0) { best = a; bi = _index[a]; }
  }
  if (bi >= 0) return { idx: bi, hack: true };
  // Chinese keywords (the collection names of repacks are often things like "action arcade" or "Famicom"; the name is kept as it is, only the icon and the category are borrowed)
  var raw = (str || "").toLowerCase();
  for (var i = 0; i < KEYWORDS.length; i++)
    if (raw.indexOf(KEYWORDS[i][0]) >= 0) return { idx: _index[KEYWORDS[i][1]], hack: true, noYear: !!KEYWORDS[i][2] };
  return { idx: -1, hack: false };
}

// [keyword, short name of the system to borrow, year unreliable (true = do not show the year)]
var KEYWORDS = [
  ["街机", "mame", true], ["街機", "mame", true],
  ["红白机", "fc", false], ["紅白機", "fc", false],
  ["超级任天堂", "sfc", false], ["超級任天堂", "sfc", false], ["超任", "sfc", false]
];

function _hex2(v) { var s = Math.round(v * 255).toString(16); return s.length < 2 ? "0" + s : s; }
// CSS-style hsl(h,s,l) -> "#rrggbb" (s and l are 0-1)
function hsl(h, s, l) {
  h = ((h % 360) + 360) % 360 / 360;
  var q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  function f(t) { t = (t + 1) % 1; if (t < 1/6) return p + (q - p) * 6 * t; if (t < 1/2) return q; if (t < 2/3) return p + (q - p) * (2/3 - t) * 6; return p; }
  return "#" + _hex2(f(h + 1/3)) + _hex2(f(h)) + _hex2(f(h - 1/3));
}
function _hex(c) { return [parseInt(c.substr(1,2),16), parseInt(c.substr(3,2),16), parseInt(c.substr(5,2),16)]; }
function _mix(a, b, t) {   // mix a towards b by t (0-1)
  var x = _hex(a), y = _hex(b);
  return "#" + _hex2((x[0] + (y[0] - x[0]) * t) / 255) + _hex2((x[1] + (y[1] - x[1]) * t) / 255) + _hex2((x[2] + (y[2] - x[2]) * t) / 255);
}
function _paint(d) {
  if (d.ac) { d.base = d.ac; d.dark = _mix(d.ac, "#000000", 0.18); d.ink = _mix(d.ac, "#1F2329", 0.55); return d; }
  if (d.fixed) { d.base = "#EE8D87"; d.dark = "#D96A66"; d.ink = "#8A2F33"; return d; }
  d.base = hsl(d.hue, 0.72, 0.75); d.dark = hsl(d.hue, 0.60, 0.62); d.ink = hsl(d.hue, 0.45, 0.28);
  return d;
}

// Widescreen systems (layout uses 16:9); the others use the 4:3 layout. Can be overridden by "r":"16:9" or "4:3" in overrides.json.
var WIDE = {};   // the layout is always 4:3 (video frame)
// Each category shares one icon (assets/icons/cat_*.png); the glow still follows the system colour
var CAT_ICON = { home: "cat_home", hand: "cat_hand", arcade: "cat_arcade", computer: "cat_computer", other: "cat_other", multi: "cat_other", fav: "cat_fav" };
var LETTERBOX = { psp: true, gc: true, dc: true };   // native 16:9: black bars above and below inside the video/screenshot frame

// the "Favorites" virtual system (borrows the generic gamepad icon; sorted first; yellow tones)
function favDesc(name) {
  return { icon: "fav", cicon: "cat_fav", name: name, cat: "fav", year: 0, hue: 46, fixed: false, pre: 1, wide: false, virtual: true, letterbox: false,
           ac: "#FF3B5C", base: "#FF3B5C", dark: "#D92F4D", ink: "#6B1E2D" };
}

function _hash(s) {
  var h = 7;
  for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) % 100003;
  return h;
}

// User overrides (optional): keyed by the lower-cased shortName, may contain { y, c, k, n }; read from overrides.json by theme.qml and passed in
function describe(shortName, name, overrides) {
  var r = _lookup(shortName);
  if (r.idx < 0) r = _lookup(name);
  var d;
  if (r.idx >= 0) {
    var e = CATALOG[r.idx];
    d = { icon: e.k, name: r.hack ? (name || "").replace(/^arcade\s*-\s*/i, "") : e.n, cat: e.c, year: e.y, ac: e.ac, iconScale: e.is || 1,
          hue: (3 + r.idx * 137) % 360, fixed: e.k === "fc" && !r.hack, pre: r.idx };
    if (r.hack) d.hue = (3 + r.idx * 137 + 20) % 360;
    if (r.noYear) d.year = 0;
  } else {
    d = { icon: "retro", ac: "#8B6F47", name: name || shortName || "?", cat: "other", year: 0,
          hue: (3 + (CATALOG.length + _hash(norm(name || shortName))) * 137) % 360, fixed: false,
          pre: CATALOG.length + _hash(norm(name || shortName)) };
  }
  var o = overrides && overrides[(shortName || "").toLowerCase()];
  if (o) {
    if (o.k) d.icon = o.k;
    if (o.n) d.name = o.n;
    if (o.c && CAT_ORDER[o.c] !== undefined) d.cat = o.c;
    if (o.y !== undefined) d.year = o.y;
    if (o.r) d.wide = (o.r === "16:9");
  }
  if (d.wide === undefined) d.wide = !!WIDE[d.icon];
  d.letterbox = !!LETTERBOX[d.icon];
  d.cicon = CAT_ICON[d.cat] || "cat_other";
  return _paint(d);
}

// descs: array of describe() results (same order as the collection index); returns the collection indices sorted by the rules
function sortedOrder(descs) {
  var idx = [];
  for (var i = 0; i < descs.length; i++) idx.push(i);
  idx.sort(function (a, b) {
    var da = descs[a], db = descs[b];
    if (CAT_ORDER[da.cat] !== CAT_ORDER[db.cat]) return CAT_ORDER[da.cat] - CAT_ORDER[db.cat];
    var ya = da.year || 9999, yb = db.year || 9999;
    if (ya !== yb) return ya - yb;
    return da.name < db.name ? -1 : (da.name > db.name ? 1 : a - b);
  });
  return idx;
}

// three groups of polygon backgrounds (percentage coordinates): [kind (a=white .20, b=dark .22, c=white .12), [x,y,...]]
var PRESETS = [
  [["a", [0,0, 46,0, 30,34, 0,48]], ["b", [46,0, 100,0, 100,40, 64,22]], ["c", [30,34, 64,22, 58,58, 22,70]],
   ["b", [0,48, 30,34, 22,70, 0,82]], ["a", [58,58, 100,40, 100,100, 70,100]], ["b", [0,82, 22,70, 70,100, 0,100]]],
  [["b", [0,0, 60,0, 40,30, 0,20]], ["a", [60,0, 100,0, 100,55, 70,45, 40,30]], ["c", [0,20, 40,30, 70,45, 50,80, 0,70]],
   ["a", [0,70, 50,80, 40,100, 0,100]], ["b", [70,45, 100,55, 100,100, 40,100, 50,80]]],
  [["a", [0,0, 100,0, 100,28, 35,38, 0,24]], ["b", [0,24, 35,38, 20,74, 0,66]], ["c", [35,38, 100,28, 100,66, 60,70]],
   ["b", [60,70, 100,66, 100,100, 45,100]], ["a", [20,74, 60,70, 45,100, 0,100, 0,66]]]
];

function preset(i) { return PRESETS[((i % 3) + 3) % 3]; }

// ================= background: triangle-grid colour patches (parameters tuned with facet_tool.html) =================
// layer space 960x600; the middle area of the system page takes the left 684 width, the game page takes 960x440.
var FACET = { cell: 48, jitter: 0.4, quadP: 0, pentP: 0, contrast: 0.55, grad: 0.45, gang: 320,
              wmax: 0.6, dmax: 0.5, mode: "smooth", seed: 8202, seedStep: 977 };

function _rngf(seed) {
  var a = seed >>> 0;
  return function () {
    a |= 0; a = a + 0x6D2B79F5 | 0;
    var t = Math.imul(a ^ a >>> 15, 1 | a);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

var _facetCache = {};
// triangle grid -> jitter -> randomly merged into rhombi/pentagons -> light/dark tone in [-1,1] from a "light gradient + random"
function facetPolys(seed) {
  if (_facetCache[seed]) return _facetCache[seed];
  var P = FACET, W = 960, H = 600, r = _rngf(seed);
  var cw = W / Math.max(4, Math.round(W / P.cell)), rh = cw * 0.866;
  var rows = Math.ceil(H / rh) + 2, cols = Math.ceil(W / cw) + 2;
  var V = {};
  function vid(j, i) { return j + "," + i; }
  var i, j, k, e;
  for (j = -1; j < rows; j++)
    for (i = -1; i < cols; i++)
      V[vid(j, i)] = [(i + (((j % 2) + 2) % 2) * 0.5) * cw + (r() - 0.5) * P.jitter * cw,
                      j * rh + (r() - 0.5) * P.jitter * rh * 1.1];
  var T = [];
  for (j = -1; j < rows - 1; j++)
    for (i = -1; i < cols - 1; i++) {
      if (((j % 2) + 2) % 2 === 0) {
        T.push([vid(j, i), vid(j, i + 1), vid(j + 1, i)]);
        T.push([vid(j, i + 1), vid(j + 1, i + 1), vid(j + 1, i)]);
      } else {
        T.push([vid(j, i), vid(j + 1, i + 1), vid(j + 1, i)]);
        T.push([vid(j, i), vid(j, i + 1), vid(j + 1, i + 1)]);
      }
    }
  function ek(a, b) { return a < b ? a + "|" + b : b + "|" + a; }
  var E = {};
  for (k = 0; k < T.length; k++)
    for (e = 0; e < 3; e++) { var key = ek(T[k][e], T[k][(e + 1) % 3]); (E[key] = E[key] || []).push(k); }
  function nb(k) {
    var o = [];
    for (var e = 0; e < 3; e++) {
      var l = E[ek(T[k][e], T[k][(e + 1) % 3])] || [];
      for (var q = 0; q < l.length; q++) if (l[q] !== k) o.push(l[q]);
    }
    return o;
  }
  var order = [];
  for (k = 0; k < T.length; k++) order.push(k);
  for (k = order.length - 1; k > 0; k--) { var q2 = Math.floor(r() * (k + 1)); var tmp = order[k]; order[k] = order[q2]; order[q2] = tmp; }
  var used = {}, polys = [];
  function ring(tris) {
    var cnt = {}, t, a, b, n = 0, next = {};
    for (t = 0; t < tris.length; t++) for (e = 0; e < 3; e++) { a = T[tris[t]][e]; b = T[tris[t]][(e + 1) % 3]; cnt[ek(a, b)] = (cnt[ek(a, b)] || 0) + 1; }
    for (t = 0; t < tris.length; t++) for (e = 0; e < 3; e++) { a = T[tris[t]][e]; b = T[tris[t]][(e + 1) % 3]; if (cnt[ek(a, b)] === 1) { next[a] = b; n++; } }
    var start = null;
    for (var s in next) { start = s; break; }
    if (start === null) return null;
    var out = [start], cur = next[start];
    while (cur !== start && out.length <= n) { out.push(cur); cur = next[cur]; if (cur === undefined) return null; }
    return (out.length === n && cur === start) ? out : null;
  }
  function unusedNb(k) { return nb(k).filter(function (x) { return !used[x]; }); }
  for (var oi = 0; oi < order.length; oi++) {
    k = order[oi];
    if (used[k]) continue;
    var roll = r(), group = [k];
    if (roll < P.pentP) {
      var n1 = unusedNb(k);
      if (n1.length) {
        var a1 = n1[Math.floor(r() * n1.length)];
        var n2 = nb(k).concat(nb(a1)).filter(function (x) { return x !== k && x !== a1 && !used[x]; });
        if (n2.length) group = [k, a1, n2[Math.floor(r() * n2.length)]];
      }
    } else if (roll < P.pentP + P.quadP) {
      var m1 = unusedNb(k);
      if (m1.length) group = [k, m1[Math.floor(r() * m1.length)]];
    }
    var ids = ring(group);
    if (!ids) { group = [k]; ids = T[k]; }
    for (var g = 0; g < group.length; g++) used[group[g]] = true;
    var pts = [];
    for (var pi = 0; pi < ids.length; pi++) pts.push(V[ids[pi]]);
    polys.push({ pts: pts, tone: 0 });
  }
  var ang = P.gang * Math.PI / 180, gx = Math.cos(ang), gy = Math.sin(ang);
  for (var pj = 0; pj < polys.length; pj++) {
    var p = polys[pj], sx = 0, sy = 0;
    for (var q = 0; q < p.pts.length; q++) { sx += p.pts[q][0]; sy += p.pts[q][1]; }
    var cx = sx / p.pts.length / W, cy = sy / p.pts.length / H;
    var v = ((cx - 0.5) * gx + (cy - 0.5) * gy) * 2 * P.grad + (r() - 0.5) * 2 * P.contrast;
    v = Math.max(-1, Math.min(1, v));
    if (P.mode === "3") v = v < -0.33 ? -1 : (v > 0.33 ? 1 : 0);
    p.tone = v;
  }
  _facetCache[seed] = polys;
  return polys;
}

// each system has one fixed pattern (FC is the one that was tuned)
function facetSeed(desc) { return FACET.seed + ((desc && desc.pre) || 0) * FACET.seedStep; }

function _rgb(h) { return [parseInt(h.substr(1, 2), 16), parseInt(h.substr(3, 2), 16), parseInt(h.substr(5, 2), 16)]; }
function _mixHex(a, b, t) {
  var o = [];
  for (var i = 0; i < 3; i++) o.push(Math.round(a[i] + (b[i] - a[i]) * t));
  return "rgb(" + o[0] + "," + o[1] + "," + o[2] + ")";
}
// final opaque colour of a polygon
function facetFill(tone, baseHex, darkHex) {
  var b = _rgb(baseHex);
  return tone > 0 ? _mixHex(b, [255, 255, 255], tone * FACET.wmax) : _mixHex(b, _rgb(darkHex), -tone * FACET.dmax);
}
