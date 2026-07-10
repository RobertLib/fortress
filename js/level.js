// Text-file level format:
//   Lines starting with ";" are comments.
//   Lines starting with "@" are properties: @name, @floor, @ceil (colors).
//   Everything else is the map grid, one character per cell:
//     1-6    walls (texture variants), # = 1
//     B      barred wall (solid, but can be seen and shot through)
//     .      floor
//     D      door (slides open)
//     R      barred door (slides open, can be seen and shot through)
//     X      exit door (needs all 3 keys)
//     P      player start (append facing to @start: N/S/E/W, default E)
//     K      key            H healing potion (+25)   F bread (+10)
//     A      quiver of bolts   T treasure (score)    M repeating crossbow
//     G/g    guard (patrol/standing)
//     S/s    knight         O/o captain
//     V/v    bat swarm (flying about / roosting until disturbed)
//     Wall torches are placed automatically on suitable stretches of masonry.
//     ^      arrow trap: a pressure plate on the floor; the nearest wall in
//            a straight line becomes an arrow slit (texture 7) and looses a
//            volley of arrows when the plate is stepped on

export const WALLS = new Set(["1", "2", "3", "4", "5", "6", "7", "B", "#"]);

const ITEM_CHARS = { K: "key", H: "potion", F: "bread", A: "bolts", T: "treasure", M: "arbalest" };
const ENEMY_CHARS = { G: "guard", S: "knight", O: "captain", V: "bat" };

export function parseLevel(text) {
  const props = { name: "FLOOR", floor: "#707070", ceil: "#383838", start: "E" };
  const rows = [];
  for (const raw of text.split(/\r?\n/)) {
    if (raw.startsWith(";")) continue;
    if (raw.startsWith("@")) {
      const m = raw.match(/^@(\w+)\s+(.*)$/);
      if (m) props[m[1]] = m[2].trim();
      continue;
    }
    if (raw.trim() === "") continue;
    rows.push(raw);
  }
  const h = rows.length;
  const w = Math.max(...rows.map((r) => r.length));

  const grid = new Array(w * h).fill("1");
  const items = [];
  const enemies = [];
  const doors = [];
  const trapPlates = [];
  let player = null;

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let ch = rows[y][x] ?? "1";
      if (ch === " ") ch = "1"; // padding counts as wall
      if (ch === "#") ch = "1";
      let cell = ".";
      if (WALLS.has(ch)) {
        cell = ch;
      } else if (ch === "D" || ch === "R") {
        cell = ch;
        doors.push({ x, y, kind: ch });
      } else if (ch === "X") {
        cell = "X";
      } else if (ch === "P") {
        player = { x: x + 0.5, y: y + 0.5 };
      } else if (ch === "^") {
        trapPlates.push({ x, y });
      } else if (ITEM_CHARS[ch]) {
        items.push({ x: x + 0.5, y: y + 0.5, kind: ITEM_CHARS[ch] });
      } else if (ENEMY_CHARS[ch] || ENEMY_CHARS[ch.toUpperCase()]) {
        const type = ENEMY_CHARS[ch.toUpperCase()];
        enemies.push({ x: x + 0.5, y: y + 0.5, type, patrol: ch === ch.toUpperCase() });
      }
      grid[y * w + x] = cell;
    }
  }

  // Resolve each trap's shooter: the nearest wall in a cardinal direction
  // with a clear run of floor becomes an arrow slit (texture "7"). Slits at
  // least 2 cells out are preferred so the volley is visible in flight.
  const traps = [];
  for (const t of trapPlates) {
    let best = null;
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      for (let d = 1; d <= 8; d++) {
        const cx = t.x + dx * d;
        const cy = t.y + dy * d;
        if (cx < 0 || cy < 0 || cx >= w || cy >= h) break;
        const cell = grid[cy * w + cx];
        if (cell === ".") continue;
        if (WALLS.has(cell)) {
          const rank = (d >= 2 ? 0 : 100) + d;
          if (!best || rank < best.rank) best = { rank, cx, cy, dx, dy };
        }
        break; // doors and the gate can't hide a slit
      }
    }
    if (!best) continue; // walled-in plate: nowhere to shoot from, drop it
    grid[best.cy * w + best.cx] = "7";
    traps.push({
      x: t.x,
      y: t.y,
      dirX: -best.dx,
      dirY: -best.dy,
      originX: best.cx + 0.5 - best.dx * 0.51,
      originY: best.cy + 0.5 - best.dy * 0.51,
    });
  }

  // Scatter a modest number of torches along walls that border walkable
  // space. Sorting by a coordinate hash makes the layout stable while the
  // spacing pass keeps corridors from turning into rows of identical lights.
  const torchCandidates = [];
  const dirsOut = [[1, 0], [-1, 0], [0, 1], [0, -1]];
  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const cell = grid[y * w + x];
      if (!WALLS.has(cell) || cell === "7" || cell === "B") continue;
      for (let d = 0; d < dirsOut.length; d++) {
        const [dx, dy] = dirsOut[d];
        if (grid[(y + dy) * w + x + dx] !== ".") continue;
        const hash = Math.imul(x + 17, 73856093) ^ Math.imul(y + 31, 19349663) ^ Math.imul(d + 7, 83492791);
        torchCandidates.push({ x, y, dx, dy, hash: hash >>> 0 });
      }
    }
  }
  torchCandidates.sort((a, b) => a.hash - b.hash);
  const torches = [];
  const torchLimit = Math.max(7, Math.min(14, Math.round((w * h) / 70)));
  for (const c of torchCandidates) {
    const tx = c.x + 0.5 + c.dx * 0.52;
    const ty = c.y + 0.5 + c.dy * 0.52;
    if (torches.some((t) => (t.x - tx) ** 2 + (t.y - ty) ** 2 < 18)) continue;
    torches.push({ x: tx, y: ty, phase: (c.hash % 1000) / 1000 * Math.PI * 2 });
    if (torches.length >= torchLimit) break;
  }

  if (!player) throw new Error("Level has no player start (P)");
  const dirs = { N: [0, -1], S: [0, 1], E: [1, 0], W: [-1, 0] };
  const [dx, dy] = dirs[props.start] ?? dirs.E;

  return {
    name: props.name,
    floorColor: props.floor,
    ceilColor: props.ceil,
    w,
    h,
    grid,
    items,
    enemies,
    doors,
    traps,
    torches,
    playerStart: { ...player, dx, dy },
    keyCount: items.filter((i) => i.kind === "key").length,
    treasureCount: items.filter((i) => i.kind === "treasure").length,
  };
}

export async function loadLevels(count) {
  const levels = [];
  for (let i = 1; i <= count; i++) {
    const res = await fetch(`levels/${i}.txt`);
    if (!res.ok) throw new Error(`Failed to load levels/${i}.txt (${res.status})`);
    levels.push(parseLevel(await res.text()));
  }
  return levels;
}
