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
//     C/c    war hound (roaming/staying put). A hound with a soldier within
//            4 cells is leashed to him instead: it heels to its handler,
//            barks him awake when it spots the player, and returns to his
//            side when it loses the chase.
//     Wall torches are placed automatically on suitable stretches of masonry.
//     Tables and chairs are likewise furnished automatically in larger rooms,
//     and wardrobes and chests are set flush against flat wall stretches.
//     A fountain plays in the widest court, wherever the floor opens up
//     enough to hold its basin.
//     ^      arrow trap: a pressure plate on the floor; the nearest wall in
//            a straight line becomes an arrow slit (texture 7) and looses a
//            volley of arrows when the plate is stepped on
//     Z      secret wall: looks like the surrounding masonry, slides open
//            when the nearest lever is pulled and then stays open
//     L      lever on a wall: press use on it to open its secret wall(s).
//            The lever shows on exactly one face of the cell — one that
//            looks out on floor the player can reach without any secrets,
//            so it can never end up buried or sealed away; the other faces
//            wear the surrounding masonry.

export const WALLS = new Set(["1", "2", "3", "4", "5", "6", "7", "B", "#"]);

const ITEM_CHARS = { K: "key", H: "potion", F: "bread", A: "bolts", T: "treasure", M: "arbalest" };
const ENEMY_CHARS = { G: "guard", S: "knight", O: "captain", V: "bat", C: "dog" };

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
  const levers = [];
  const secretCells = [];
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
      } else if (ch === "Z") {
        cell = "Z";
        secretCells.push({ x, y });
      } else if (ch === "L") {
        cell = "L";
        levers.push({ x, y, doors: [] });
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

  // Every war hound is leashed to the nearest man-at-arms within earshot
  // (4 cells): it heels to him, springs when he fights and barks him awake
  // in turn. A hound with no soldier nearby just guards its own ground.
  for (const dog of enemies) {
    if (dog.type !== "dog") continue;
    let best = null;
    for (let i = 0; i < enemies.length; i++) {
      const e = enemies[i];
      if (e.type === "dog" || e.type === "bat") continue;
      const d = (e.x - dog.x) ** 2 + (e.y - dog.y) ** 2;
      if (d <= 16 && (!best || d < best.d)) best = { d, i };
    }
    if (best) dog.master = best.i;
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

  // Secret walls become sliding doors in disguise: each takes the texture
  // most common among its plain-wall neighbours so it melts into the
  // masonry, and is wired to the nearest lever, which will open it.
  for (const s of secretCells) {
    const counts = {};
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const c = grid[(s.y + dy) * w + s.x + dx];
      if (c && "123456".includes(c)) counts[c] = (counts[c] ?? 0) + 1;
    }
    const tex = Object.keys(counts).sort((a, b) => counts[b] - counts[a])[0] ?? "1";
    doors.push({ x: s.x, y: s.y, kind: "Z", tex });
    let best = null;
    for (const l of levers) {
      const d = (l.x - s.x) ** 2 + (l.y - s.y) ** 2;
      if (!best || d < best.d) best = { d, lever: l };
    }
    if (best) best.lever.doors.push(s.y * w + s.x);
    else console.warn(`Secret wall at ${s.x},${s.y} has no lever — it will never open`);
  }

  // Give each lever its one face. Only floor the player can actually walk to
  // qualifies (flood fill with secrets still closed), so the handle can never
  // face into a wall, a sealed stash or an unreachable pocket. With several
  // reachable faces the one nearest the lever's secret wall wins — that is
  // where the player will be hunting. The other faces of the cell wear the
  // neighbours' masonry, like the secret walls do.
  const cardinal = [[1, 0], [-1, 0], [0, 1], [0, -1]];
  const reachable = new Uint8Array(w * h);
  if (player && levers.length) {
    const stack = [Math.floor(player.y) * w + Math.floor(player.x)];
    reachable[stack[0]] = 1;
    while (stack.length) {
      const idx = stack.pop();
      const ix = idx % w;
      const iy = (idx - ix) / w;
      for (const [dx, dy] of cardinal) {
        const nx = ix + dx;
        const ny = iy + dy;
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        const nIdx = ny * w + nx;
        if (reachable[nIdx]) continue;
        const c = grid[nIdx];
        if (c !== "." && c !== "D" && c !== "R") continue;
        reachable[nIdx] = 1;
        stack.push(nIdx);
      }
    }
  }
  for (const l of levers) {
    const counts = {};
    for (const [dx, dy] of cardinal) {
      const c = grid[(l.y + dy) * w + l.x + dx];
      if (c && "123456".includes(c)) counts[c] = (counts[c] ?? 0) + 1;
    }
    l.tex = Object.keys(counts).sort((a, b) => counts[b] - counts[a])[0] ?? "1";
    const doorKey = l.doors[0];
    const doorX = doorKey != null ? doorKey % w : l.x;
    const doorY = doorKey != null ? (doorKey - doorX) / w : l.y;
    let face = null;
    for (const [dx, dy] of cardinal) {
      const nx = l.x + dx;
      const ny = l.y + dy;
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
      if (grid[ny * w + nx] !== "." || !reachable[ny * w + nx]) continue;
      const d = (nx - doorX) ** 2 + (ny - doorY) ** 2;
      if (!face || d < face.d) face = { d, dx, dy };
    }
    if (!face) {
      console.warn(`Lever at ${l.x},${l.y} faces no reachable floor — it cannot be pulled`);
      face = { dx: 1, dy: 0 };
    }
    l.dx = face.dx;
    l.dy = face.dy;
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

  // Furnish the larger rooms: a table wherever the floor opens up wide, with
  // a chair or two pulled to its sides. A spot qualifies when its 3x3 core is
  // clear of walls, spawns and pickups and most of the surrounding 5x5 is
  // open floor — corridors and crossings never pass that test. Openness-first
  // ordering fills the biggest halls; the hash keeps ties stable.
  const spawnCells = new Set();
  const markSpawn = (sx, sy) => spawnCells.add(Math.floor(sy) * w + Math.floor(sx));
  if (player) markSpawn(player.x, player.y);
  for (const i of items) markSpawn(i.x, i.y);
  for (const e of enemies) markSpawn(e.x, e.y);
  for (const t of trapPlates) spawnCells.add(t.y * w + t.x);

  const isFloor = (x, y) => x >= 0 && y >= 0 && x < w && y < h && grid[y * w + x] === ".";
  const tableSpots = [];
  for (let y = 2; y < h - 2; y++) {
    for (let x = 2; x < w - 2; x++) {
      let coreOk = true;
      for (let dy = -1; dy <= 1 && coreOk; dy++) {
        for (let dx = -1; dx <= 1; dx++) {
          // walls must clear the whole 3x3; spawns and pickups only matter on
          // the table cell itself and the orthogonal cells chairs may take
          if (!isFloor(x + dx, y + dy) || (dx * dy === 0 && spawnCells.has((y + dy) * w + x + dx))) {
            coreOk = false;
            break;
          }
        }
      }
      if (!coreOk) continue;
      let open = 0;
      for (let dy = -2; dy <= 2; dy++) {
        for (let dx = -2; dx <= 2; dx++) if (isFloor(x + dx, y + dy)) open++;
      }
      if (open < 16) continue;
      const hash = (Math.imul(x + 41, 2654435761) ^ Math.imul(y + 89, 97370749)) >>> 0;
      tableSpots.push({ x, y, open, hash });
    }
  }
  tableSpots.sort((a, b) => b.open - a.open || a.hash - b.hash);
  const decorations = [];

  // A stone fountain graces the widest court. It takes the very best spots
  // before the furniture is brought in, demands nearly all of its 5x5 to be
  // open floor — so cramped keeps simply go without — and the tables then
  // keep a respectful distance from its basin.
  const fountainLimit = w * h >= 900 ? 2 : 1;
  let fountainsPlaced = 0;
  for (const s of tableSpots) {
    if (fountainsPlaced >= fountainLimit) break;
    if (s.open < 19) continue;
    const cx = s.x + 0.5;
    const cy = s.y + 0.5;
    if (decorations.some((d) => (d.x - cx) ** 2 + (d.y - cy) ** 2 < 100)) continue;
    decorations.push({ x: cx, y: cy, kind: "fountain", radius: 0.42 });
    fountainsPlaced++;
  }

  const tableLimit = Math.max(2, Math.min(6, Math.round((w * h) / 110)));
  let tables = 0;
  for (const s of tableSpots) {
    if (tables >= tableLimit) break;
    const cx = s.x + 0.5;
    const cy = s.y + 0.5;
    const crowded = decorations.some((d) => {
      const sq = (d.x - cx) ** 2 + (d.y - cy) ** 2;
      return sq < (d.kind === "table" ? 22 : d.kind === "fountain" ? 8 : 0);
    });
    if (crowded) continue;
    decorations.push({ x: cx, y: cy, kind: "table", radius: 0.36 });
    tables++;
    // chairs sit in the (guaranteed clear) neighbour cells, pulled to the table
    const chairCount = 1 + ((s.hash >>> 3) & 1);
    for (let k = 0; k < chairCount; k++) {
      const [dx, dy] = dirsOut[(s.hash + k) % 4];
      decorations.push({ x: cx + dx * 0.85, y: cy + dy * 0.85, kind: "chair", radius: 0.18 });
    }
  }

  // Wall boxes (wardrobes and chests) lean against flat wall stretches. The
  // wall cell keeps its own texture — the renderer draws the box in front of
  // it — so masonry tiling is never disturbed. A spot needs a plain wall
  // flanked by more plain wall (so the box sits flush, with no door/bars/slit
  // at its edges), open floor two cells deep in front (never plugs a
  // corridor), and clearance from torches and furniture.
  const wallBoxes = [];
  const boxCandidates = [];
  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      if (!"123456".includes(grid[y * w + x])) continue;
      for (let d = 0; d < dirsOut.length; d++) {
        const [dx, dy] = dirsOut[d];
        const fx = x + dx;
        const fy = y + dy;
        if (grid[fy * w + fx] !== "." || spawnCells.has(fy * w + fx)) continue;
        if (!isFloor(fx + dx, fy + dy)) continue;
        const flankA = grid[(y + dx) * w + (x - dy)];
        const flankB = grid[(y - dx) * w + (x + dy)];
        if (!"1234567".includes(flankA) || !"1234567".includes(flankB)) continue;
        const hash = (Math.imul(x + 59, 374761393) ^ Math.imul(y + 23, 668265263) ^ Math.imul(d + 3, 40503)) >>> 0;
        boxCandidates.push({ x, y, dx, dy, cell: grid[y * w + x], hash, used: false });
      }
    }
  }
  boxCandidates.sort((a, b) => a.hash - b.hash);
  // backing: wall textures the box may stand against — a tall wardrobe would
  // awkwardly cover the window of "4" or the banner of "6", a low chest only
  // clashes with the banner
  const placeBoxes = (kind, limit, radius, backing) => {
    let placed = 0;
    for (const c of boxCandidates) {
      if (placed >= limit) break;
      if (c.used || !backing.includes(c.cell)) continue;
      // collision circle sits on the wall face itself — half of it buried in
      // the wall — so it barely pokes past the box front and leaves corridors
      // walkable
      const cx = c.x + 0.5 + c.dx * 0.5;
      const cy = c.y + 0.5 + c.dy * 0.5;
      if (torches.some((t) => (t.x - cx) ** 2 + (t.y - cy) ** 2 < 1.7)) continue;
      if (decorations.some((dd) => (dd.x - cx) ** 2 + (dd.y - cy) ** 2 < 1.45)) continue;
      const near = wallBoxes.some((b) => {
        const sq = (b.x + 0.5 - cx) ** 2 + (b.y + 0.5 - cy) ** 2;
        return sq < (b.kind === kind ? 9 : 2.56);
      });
      if (near) continue;
      c.used = true;
      wallBoxes.push({ x: c.x, y: c.y, dx: c.dx, dy: c.dy, kind });
      decorations.push({ x: cx, y: cy, kind, radius });
      placed++;
    }
  };
  placeBoxes("wardrobe", Math.max(2, Math.min(5, Math.round((w * h) / 130))), 0.28, "1235");
  placeBoxes("chest", Math.max(2, Math.min(4, Math.round((w * h) / 160))), 0.28, "12345");

  // A suit of armour stands against the wall, rendered as a billboard like
  // the furniture, so it keeps its presence from every angle. Pure set
  // dressing: no collision circle, so it can never block a corridor.
  const armorLimit = Math.max(1, Math.min(3, Math.round((w * h) / 220)));
  let armors = 0;
  for (const c of boxCandidates) {
    if (armors >= armorLimit) break;
    if (c.used || !"1235".includes(c.cell)) continue;
    const cx = c.x + 0.5 + c.dx * 0.7;
    const cy = c.y + 0.5 + c.dy * 0.7;
    if (torches.some((t) => (t.x - cx) ** 2 + (t.y - cy) ** 2 < 1.7)) continue;
    if (decorations.some((dd) => (dd.x - cx) ** 2 + (dd.y - cy) ** 2 < 2.56)) continue;
    c.used = true;
    decorations.push({ x: cx, y: cy, kind: "armor", radius: 0 });
    armors++;
  }

  // Crossed swords and shields hang flat on the masonry.
  const hangings = [];
  const placeHangings = (kind, limit) => {
    let placed = 0;
    for (const c of boxCandidates) {
      if (placed >= limit) break;
      if (c.used || !"1235".includes(c.cell)) continue;
      const cx = c.x + 0.5 + c.dx * 0.6;
      const cy = c.y + 0.5 + c.dy * 0.6;
      if (torches.some((t) => (t.x - cx) ** 2 + (t.y - cy) ** 2 < 1.7)) continue;
      if (decorations.some((dd) => (dd.x - cx) ** 2 + (dd.y - cy) ** 2 < 1.45)) continue;
      const near = hangings.some((hh) => {
        const sq = (hh.x + 0.5 - cx) ** 2 + (hh.y + 0.5 - cy) ** 2;
        return sq < (hh.kind === kind ? 9 : 2.56);
      });
      if (near) continue;
      c.used = true;
      hangings.push({ x: c.x, y: c.y, dx: c.dx, dy: c.dy, kind });
      placed++;
    }
  };
  placeHangings("shield", Math.max(1, Math.min(3, Math.round((w * h) / 200))));
  placeHangings("sword", Math.max(1, Math.min(3, Math.round((w * h) / 200))));

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
    levers,
    secretCount: levers.filter((l) => l.doors.length > 0).length,
    torches,
    decorations,
    wallBoxes,
    hangings,
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
