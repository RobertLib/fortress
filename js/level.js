// Text-file level format:
//   Lines starting with ";" are comments.
//   Lines starting with "@" are properties: @name, @floor, @ceil (colors).
//   Everything else is the map grid, one character per cell:
//     1-6    walls (texture variants), # = 1
//     .      floor
//     D      door (slides open)
//     X      exit door (needs all 3 keys)
//     P      player start (append facing to @start: N/S/E/W, default E)
//     K      key            H healing potion (+25)   F bread (+10)
//     A      quiver of bolts   T treasure (score)    M repeating crossbow
//     G/g    guard (patrol/standing)
//     S/s    knight         O/o captain

export const WALLS = new Set(["1", "2", "3", "4", "5", "6", "#"]);

const ITEM_CHARS = { K: "key", H: "potion", F: "bread", A: "bolts", T: "treasure", M: "arbalest" };
const ENEMY_CHARS = { G: "guard", S: "knight", O: "captain" };

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
  let player = null;

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let ch = rows[y][x] ?? "1";
      if (ch === " ") ch = "1"; // padding counts as wall
      if (ch === "#") ch = "1";
      let cell = ".";
      if (WALLS.has(ch)) {
        cell = ch;
      } else if (ch === "D") {
        cell = "D";
        doors.push({ x, y });
      } else if (ch === "X") {
        cell = "X";
      } else if (ch === "P") {
        player = { x: x + 0.5, y: y + 0.5 };
      } else if (ITEM_CHARS[ch]) {
        items.push({ x: x + 0.5, y: y + 0.5, kind: ITEM_CHARS[ch] });
      } else if (ENEMY_CHARS[ch] || ENEMY_CHARS[ch.toUpperCase()]) {
        const type = ENEMY_CHARS[ch.toUpperCase()];
        enemies.push({ x: x + 0.5, y: y + 0.5, type, patrol: ch === ch.toUpperCase() });
      }
      grid[y * w + x] = cell;
    }
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
