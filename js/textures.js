// Procedural pixel-art: wall textures, item/enemy sprites, weapon views, HUD
// crest. Everything is rendered ONCE into offscreen canvases at startup.

const TEX = 64;

function canvas(w = TEX, h = TEX) {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  return c;
}

function rng(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

function speckle(g, rand, color, count, alpha = 0.25) {
  g.save();
  g.globalAlpha = alpha;
  g.fillStyle = color;
  for (let i = 0; i < count; i++) {
    g.fillRect(Math.floor(rand() * TEX), Math.floor(rand() * TEX), 1 + Math.floor(rand() * 2), 1);
  }
  g.restore();
}

export function darken(src, factor) {
  const c = canvas(src.width, src.height);
  const g = c.getContext("2d");
  g.drawImage(src, 0, 0);
  g.globalCompositeOperation = "multiply";
  const v = Math.floor(factor * 255);
  g.fillStyle = `rgb(${v},${v},${v})`;
  g.fillRect(0, 0, src.width, src.height);
  // restore alpha (multiply fills transparent areas too)
  g.globalCompositeOperation = "destination-in";
  g.drawImage(src, 0, 0);
  return c;
}

function shadeColor(hex, f) {
  const n = parseInt(hex.slice(1), 16);
  const r = Math.min(255, Math.floor(((n >> 16) & 255) * f));
  const g = Math.min(255, Math.floor(((n >> 8) & 255) * f));
  const b = Math.min(255, Math.floor((n & 255) * f));
  return `rgb(${r},${g},${b})`;
}

// ---------------------------------------------------------------- wall texs

// Rough masonry with irregular course heights and block widths.
function stoneTexture(seed, { base = "#6b6157", moss = false } = {}) {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(seed);
  g.fillStyle = "#2b2622"; // mortar
  g.fillRect(0, 0, TEX, TEX);
  let y = 0;
  while (y < TEX) {
    const rh = 10 + Math.floor(rand() * 6);
    let x = -2 - Math.floor(rand() * 8);
    while (x < TEX) {
      const bw = 12 + Math.floor(rand() * 12);
      g.fillStyle = shadeColor(base, 0.75 + rand() * 0.4);
      g.fillRect(x + 1, y + 1, bw - 2, rh - 2);
      g.fillStyle = "rgba(255,255,255,0.07)";
      g.fillRect(x + 1, y + 1, bw - 2, 2);
      g.fillStyle = "rgba(0,0,0,0.2)";
      g.fillRect(x + 1, y + rh - 2, bw - 2, 1);
      x += bw;
    }
    y += rh;
  }
  speckle(g, rand, "#000", 110, 0.16);
  speckle(g, rand, "#fff", 30, 0.05);
  if (moss) {
    g.globalAlpha = 0.55;
    for (let i = 0; i < 60; i++) {
      g.fillStyle = shadeColor("#4a6a2a", 0.6 + rand() * 0.6);
      g.fillRect(
        Math.floor(rand() * TEX),
        Math.floor(TEX * 0.25 + rand() * TEX * 0.75),
        2 + Math.floor(rand() * 5),
        1 + Math.floor(rand() * 3),
      );
    }
    g.globalAlpha = 1;
  }
  return c;
}

// Timber-framed wall: aged plaster panels between dark oak beams.
function timberTexture(seed) {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(seed);
  g.fillStyle = "#9c8f74"; // plaster
  g.fillRect(0, 0, TEX, TEX);
  g.globalAlpha = 0.2;
  for (let i = 0; i < 90; i++) {
    g.fillStyle = shadeColor("#7a6d54", 0.7 + rand() * 0.5);
    g.fillRect(Math.floor(rand() * TEX), Math.floor(rand() * TEX), 2 + Math.floor(rand() * 4), 1 + Math.floor(rand() * 2));
  }
  g.globalAlpha = 1;
  const beam = (x, y, w, h) => {
    g.fillStyle = "#46331e";
    g.fillRect(x, y, w, h);
    g.fillStyle = "rgba(255,255,255,0.08)";
    g.fillRect(x, y, w, 1);
    g.fillStyle = "rgba(0,0,0,0.3)";
    g.fillRect(x, y + h - 1, w, 1);
  };
  beam(0, 0, TEX, 6);
  beam(0, 58, TEX, 6);
  beam(0, 0, 6, TEX);
  beam(58, 0, 6, TEX);
  // diagonal brace
  for (let i = 0; i <= 12; i++) beam(4 + i * 4, 54 - i * 4, 6, 5);
  return c;
}

// Dungeon wall: dark stone with a barred window opening.
function dungeonTexture(seed) {
  const c = stoneTexture(seed, { base: "#565049" });
  const g = c.getContext("2d");
  g.fillStyle = "#0d0b08";
  g.fillRect(18, 12, 28, 26);
  g.fillStyle = "#847a6c"; // sill
  g.fillRect(16, 38, 32, 3);
  g.fillRect(16, 9, 32, 3);
  g.fillStyle = "#3a3f45";
  for (const x of [21, 28, 35, 42]) g.fillRect(x, 12, 2, 26);
  g.fillStyle = "rgba(255,255,255,0.15)";
  for (const x of [21, 28, 35, 42]) g.fillRect(x, 12, 1, 26);
  return c;
}

// Old fired brick, thin courses.
function brickTexture(base, mortar, seed) {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(seed);
  g.fillStyle = mortar;
  g.fillRect(0, 0, TEX, TEX);
  const bh = 6;
  for (let row = 0; row < TEX / bh; row++) {
    const off = row % 2 ? 10 : 0;
    for (let col = -1; col < 5; col++) {
      const x = col * 21 + off;
      g.fillStyle = shadeColor(base, 0.72 + rand() * 0.45);
      g.fillRect(x + 1, row * bh + 1, 19, bh - 2);
    }
  }
  speckle(g, rand, "#000", 90, 0.15);
  speckle(g, rand, "#ffcf9a", 30, 0.06);
  return c;
}

// Stone with a hanging swallow-tailed banner — flavor wall.
function bannerTexture(seed) {
  const c = stoneTexture(seed);
  const g = c.getContext("2d");
  g.fillStyle = "#4a3018"; // rod
  g.fillRect(16, 2, 32, 3);
  g.fillStyle = "#8c1a1a";
  g.fillRect(22, 4, 20, 42);
  // swallow tails
  g.fillRect(22, 46, 7, 9);
  g.fillRect(35, 46, 7, 9);
  g.fillStyle = "#6d1010";
  g.fillRect(22, 4, 20, 3);
  g.fillRect(22, 43, 20, 3);
  // gold emblem: fleur
  g.fillStyle = "#e9c93c";
  g.fillRect(30, 12, 4, 20);
  g.fillRect(26, 18, 12, 4);
  g.fillRect(28, 26, 8, 3);
  g.fillStyle = "#fff6b0";
  g.fillRect(30, 12, 2, 3);
  g.fillStyle = "#00000044";
  g.fillRect(22, 4, 2, 51);
  return c;
}

// Stone wall pierced by a cross-shaped arrow loophole — trap shooter.
function arrowSlitTexture(seed) {
  const c = stoneTexture(seed);
  const g = c.getContext("2d");
  // dressed-stone surround
  g.fillStyle = "#78705f";
  g.fillRect(24, 6, 16, 52);
  g.fillStyle = "rgba(255,255,255,0.08)";
  g.fillRect(24, 6, 16, 2);
  g.fillStyle = "rgba(0,0,0,0.25)";
  g.fillRect(24, 56, 16, 2);
  // the loophole itself
  g.fillStyle = "#070605";
  g.fillRect(30, 10, 4, 44);
  g.fillRect(25, 28, 14, 5);
  // worn edges catching the torchlight
  g.fillStyle = "rgba(255,255,255,0.12)";
  g.fillRect(29, 10, 1, 44);
  g.fillRect(34, 10, 1, 44);
  return c;
}

// Back wall of a niche: the renderer carves the cell's face back to half
// depth door-style, and this masonry lines the recess — the same stone in
// shadow, gloom pooling under the vault and in the corners the torchlight
// never reaches.
function nicheTexture(seed) {
  const c = stoneTexture(seed, { base: "#57504a" });
  const g = c.getContext("2d");
  g.fillStyle = "rgba(0,0,0,0.38)";
  g.fillRect(0, 0, TEX, 8);
  g.fillStyle = "rgba(0,0,0,0.2)";
  g.fillRect(0, 8, TEX, 6);
  g.fillStyle = "rgba(0,0,0,0.3)";
  g.fillRect(0, 0, 6, TEX);
  g.fillRect(TEX - 6, 0, 6, TEX);
  g.fillStyle = "rgba(0,0,0,0.15)";
  g.fillRect(6, 0, 5, TEX);
  g.fillRect(TEX - 11, 0, 5, TEX);
  g.fillStyle = "rgba(0,0,0,0.22)"; // the floor of the recess in shadow too
  g.fillRect(0, TEX - 4, TEX, 4);
  return c;
}

// Wall-mounted lever on dressed stone: an iron backplate whose wooden
// handle points up while idle and hangs down once pulled.
function leverTexture(pulled) {
  const c = stoneTexture(111);
  const g = c.getContext("2d");
  // dressed-stone panel behind the mechanism
  g.fillStyle = "#78705f";
  g.fillRect(20, 12, 24, 40);
  g.fillStyle = "rgba(255,255,255,0.08)";
  g.fillRect(20, 12, 24, 2);
  g.fillStyle = "rgba(0,0,0,0.25)";
  g.fillRect(20, 50, 24, 2);
  // iron backplate with corner rivets and the guide slot
  g.fillStyle = "#2f3338";
  g.fillRect(25, 16, 14, 32);
  g.fillStyle = "#454b52";
  g.fillRect(25, 16, 14, 2);
  g.fillStyle = "#7c828e";
  for (const [rx, ry] of [[27, 18], [35, 18], [27, 44], [35, 44]]) g.fillRect(rx, ry, 2, 2);
  g.fillStyle = "#101214";
  g.fillRect(30, 20, 4, 24);
  // pivot boss in the middle of the slot
  g.fillStyle = "#565c66";
  g.fillRect(29, 30, 6, 5);
  g.fillStyle = "#8d949c";
  g.fillRect(30, 31, 2, 2);
  // the handle: wooden shaft with a brass knob, up = armed, down = pulled
  const dir = pulled ? 1 : -1;
  g.fillStyle = "#5a3a20";
  g.fillRect(30, pulled ? 34 : 14, 4, 18);
  g.fillStyle = "#7a5230";
  g.fillRect(30, pulled ? 34 : 14, 1, 18);
  g.fillStyle = "#c9a24a";
  g.fillRect(28, 32 + dir * 19, 8, 6);
  g.fillStyle = "#e9c93c";
  g.fillRect(29, 33 + dir * 19, 3, 2);
  return c;
}

// Heavy oak door bound with iron.
function doorTexture() {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(777);
  g.fillStyle = "#4a3018";
  g.fillRect(0, 0, TEX, TEX);
  for (let x = 2; x < 62; x += 10) {
    g.fillStyle = shadeColor("#5a3c1e", 0.85 + rand() * 0.3);
    g.fillRect(x, 2, 9, 60);
    g.fillStyle = "rgba(0,0,0,0.4)";
    g.fillRect(x, 2, 1, 60);
    g.fillStyle = "rgba(40,20,5,0.5)";
    for (let i = 0; i < 4; i++) {
      g.fillRect(x + 1 + Math.floor(rand() * 7), Math.floor(rand() * 58) + 3, 1, 4 + rand() * 8);
    }
  }
  // iron bands with rivets
  for (const y of [9, 46]) {
    g.fillStyle = "#3a3f45";
    g.fillRect(2, y, 60, 8);
    g.fillStyle = "rgba(255,255,255,0.12)";
    g.fillRect(2, y, 60, 2);
    g.fillStyle = "#7c828e";
    for (let x = 7; x < 60; x += 12) g.fillRect(x, y + 3, 2, 2);
  }
  // ring handle
  g.fillStyle = "#23262b";
  g.fillRect(41, 27, 10, 10);
  g.fillStyle = "#0f1114";
  g.fillRect(43, 29, 6, 6);
  g.fillStyle = "#7c828e";
  g.fillRect(41, 27, 10, 2);
  // frame shadow
  g.fillStyle = "rgba(0,0,0,0.45)";
  g.fillRect(0, 0, 2, TEX);
  g.fillRect(62, 0, 2, TEX);
  g.fillRect(0, 0, TEX, 2);
  return c;
}

// Iron bars with a transparent opening. The renderer composites this surface
// over whatever the ray hits farther down the corridor.
function barsTexture({ door = false } = {}) {
  const c = canvas();
  const g = c.getContext("2d");

  // A dark outline gives every bar some weight, followed by a narrow highlight
  // on the torch-facing edge. Door bars have a complete moving iron frame;
  // fixed bars are seated in heavier stone-colored top and bottom rails.
  const frame = door ? "#25292d" : "#4f4a43";
  const iron = "#30353a";
  const edge = "rgba(190,200,205,0.32)";
  const shadow = "rgba(0,0,0,0.5)";

  g.fillStyle = shadow;
  g.fillRect(0, 0, TEX, 6);
  g.fillRect(0, TEX - 7, TEX, 7);
  if (door) {
    g.fillRect(0, 0, 6, TEX);
    g.fillRect(TEX - 7, 0, 7, TEX);
  }
  g.fillStyle = frame;
  g.fillRect(0, 0, TEX, 4);
  g.fillRect(0, TEX - 5, TEX, 5);
  if (door) {
    g.fillRect(0, 0, 4, TEX);
    g.fillRect(TEX - 5, 0, 5, TEX);
  }

  for (const x of [9, 20, 31, 42, 53]) {
    g.fillStyle = shadow;
    g.fillRect(x - 1, 3, 5, 57);
    g.fillStyle = iron;
    g.fillRect(x, 3, 3, 56);
    g.fillStyle = edge;
    g.fillRect(x, 4, 1, 54);
    // pointed lower tips sell the portcullis silhouette
    g.fillStyle = iron;
    g.fillRect(x + 1, 59, 1, 3);
  }
  for (const y of [19, 39]) {
    g.fillStyle = shadow;
    g.fillRect(2, y - 1, 60, 5);
    g.fillStyle = iron;
    g.fillRect(2, y, 60, 3);
    g.fillStyle = edge;
    g.fillRect(3, y, 58, 1);
  }

  if (door) {
    // latch plate and ring handle
    g.fillStyle = "#1c2023";
    g.fillRect(47, 27, 8, 11);
    g.fillStyle = "#747b80";
    g.fillRect(49, 29, 5, 2);
    g.fillRect(52, 31, 2, 5);
    g.fillRect(49, 35, 5, 2);
  }
  return c;
}

// The locked gate: stone arch, half-raised portcullis, warm light beyond.
function gateTexture() {
  const c = stoneTexture(99, { base: "#5d564e" });
  const g = c.getContext("2d");
  // dark passage with arched top
  g.fillStyle = "#0c0a08";
  g.fillRect(10, 14, 44, 50);
  g.fillRect(14, 10, 36, 4);
  g.fillRect(18, 7, 28, 3);
  // warm glow deep inside
  g.fillStyle = "rgba(200,110,30,0.22)";
  g.fillRect(14, 30, 36, 34);
  g.fillStyle = "rgba(255,170,60,0.28)";
  g.fillRect(20, 42, 24, 22);
  g.fillStyle = "rgba(255,220,120,0.3)";
  g.fillRect(26, 52, 12, 12);
  // portcullis
  g.fillStyle = "#2f3338";
  for (let x = 13; x < 52; x += 8) g.fillRect(x, 7, 3, 49);
  for (const y of [16, 30, 44]) g.fillRect(11, y, 42, 3);
  g.fillStyle = "rgba(255,255,255,0.14)";
  for (let x = 13; x < 52; x += 8) g.fillRect(x, 7, 1, 49);
  // spiked tips
  g.fillStyle = "#494f56";
  for (let x = 13; x < 52; x += 8) g.fillRect(x + 1, 56, 1, 3);
  // gold keystone
  g.fillStyle = "#c9a24a";
  g.fillRect(29, 1, 6, 5);
  g.fillStyle = "#e9c93c";
  g.fillRect(30, 2, 4, 3);
  return c;
}

// Mirror portal: a gilded frame set into the masonry around a glass of
// swirling violet light. Two spiral arms sit 180° apart and advance 45° per
// frame, so the four frames loop seamlessly. Opaque edge to edge like every
// wall texture — the renderer fogs the whole column.
function portalTexture(frame) {
  const c = stoneTexture(88, { base: "#5d564e" });
  const g = c.getContext("2d");

  // gilded frame: lit along the top and left, in shadow at the base
  g.fillStyle = "#7a5c22";
  g.fillRect(6, 4, 52, 56);
  g.fillStyle = "#c9a24a";
  g.fillRect(6, 4, 52, 3);
  g.fillRect(6, 4, 3, 56);
  g.fillStyle = "#4a3812";
  g.fillRect(6, 57, 52, 3);
  g.fillRect(55, 4, 3, 56);
  g.fillStyle = "#e9c93c"; // corner bosses
  for (const [bx, by] of [[7, 5], [53, 5], [7, 55], [53, 55]]) g.fillRect(bx, by, 4, 4);

  // the glass: a near-black void behind the swirl
  g.fillStyle = "#0b0714";
  g.fillRect(11, 9, 42, 46);

  // spiral arms wind inward, brightening toward the heart
  const rot = (frame * Math.PI) / 4;
  const shades = ["#2a1650", "#45247e", "#6a3fae", "#9a6ad2", "#c9a8ee"];
  for (let arm = 0; arm < 2; arm++) {
    for (let r = 19; r > 2; r -= 0.7) {
      const a = rot + arm * Math.PI + (19 - r) * 0.3;
      const x = Math.floor(32 + Math.cos(a) * r);
      const y = Math.floor(32 + Math.sin(a) * r * 1.1);
      if (x < 12 || x > 50 || y < 10 || y > 52) continue;
      g.fillStyle = shades[Math.min(shades.length - 1, Math.floor((19 - r) / 4))];
      g.fillRect(x, y, 2, 2);
    }
  }
  // hot core with a pale heart
  g.fillStyle = "#e8dcff";
  g.fillRect(30, 30, 4, 4);
  g.fillStyle = "#ffffff";
  g.fillRect(31, 31, 2, 2);
  // stray motes adrift in the glass, twinkling from frame to frame
  const rand = rng(880 + frame * 7);
  g.fillStyle = "#9a6ad2";
  for (let i = 0; i < 10; i++) {
    g.fillRect(12 + Math.floor(rand() * 39), 10 + Math.floor(rand() * 43), 1, 1);
  }
  return c;
}

// ---------------------------------------------------------------- sprites

// Logical 16x16 pixel grid scaled x4 into a 64x64 canvas.
function px(g, x, y, w, h, color) {
  g.fillStyle = color;
  g.fillRect(x * 4, y * 4, w * 4, h * 4);
}

// Medieval man-at-arms billboard. pose: stand | walk1 | walk2 | aim | fire |
// pain | die1 | die2 | dead. pal.style: kettle | greathelm | plume
function soldierSprite(pal, pose) {
  const c = canvas();
  const g = c.getContext("2d");

  if (pose === "dead" || pose === "die2") {
    // lying on the floor
    const y = pose === "die2" ? 11 : 13;
    px(g, 2, y, 12, 2, pal.uniform);
    px(g, 1, y, 2, 2, pal.style === "greathelm" ? pal.helmet : pal.skin); // head
    px(g, 1, y - 1, 2, 1, pal.helmet);
    px(g, 12, y + 1, 3, 1, pal.dark);
    px(g, 4, y + 1, 6, 1, "#7c1616"); // blood
    if (pose === "dead") px(g, 3, 15, 9, 1, "#5c0f0f");
    return c;
  }
  if (pose === "die1") {
    // collapsing to knees
    if (pal.style === "greathelm") {
      px(g, 6, 3, 4, 4, pal.helmet);
    } else {
      px(g, 6, 4, 4, 3, pal.skin);
      px(g, 6, 3, 4, 2, pal.helmet);
    }
    px(g, 5, 7, 6, 5, pal.uniform);
    px(g, 4, 8, 1, 3, pal.uniform);
    px(g, 11, 8, 1, 3, pal.uniform);
    px(g, 5, 12, 2, 3, pal.dark);
    px(g, 9, 12, 2, 3, pal.dark);
    px(g, 6, 8, 4, 2, "#7c1616");
    return c;
  }

  const lean = pose === "pain" ? 1 : 0;
  const bx = lean; // body x offset

  // legs
  if (pose === "walk1") {
    px(g, 5, 11, 2, 4, pal.dark);
    px(g, 9, 12, 2, 3, pal.dark);
    px(g, 4, 15, 3, 1, "#221a12");
    px(g, 9, 15, 3, 1, "#221a12");
  } else if (pose === "walk2") {
    px(g, 5, 12, 2, 3, pal.dark);
    px(g, 9, 11, 2, 4, pal.dark);
    px(g, 4, 15, 3, 1, "#221a12");
    px(g, 9, 15, 3, 1, "#221a12");
  } else {
    px(g, 5 + bx, 11, 2, 4, pal.dark);
    px(g, 9 + bx, 11, 2, 4, pal.dark);
    px(g, 4 + bx, 15, 3, 1, "#221a12");
    px(g, 9 + bx, 15, 3, 1, "#221a12");
  }

  // torso (tabard / jerkin / breastplate)
  px(g, 5 + bx, 6, 6, 5, pal.uniform);
  px(g, 6 + bx, 6, 4, 1, pal.trim); // collar
  px(g, 7 + bx, 8, 2, 2, pal.trim); // belt buckle / emblem
  if (pal.style === "greathelm") {
    // pauldrons
    px(g, 4 + bx, 6, 2, 2, pal.helmet);
    px(g, 10 + bx, 6, 2, 2, pal.helmet);
  }

  // head
  if (pal.style === "greathelm") {
    px(g, 6 + bx, 1, 4, 4, pal.helmet);
    px(g, 5 + bx, 2, 1, 3, pal.helmet);
    px(g, 10 + bx, 2, 1, 3, pal.helmet);
    px(g, 6 + bx, 3, 4, 1, "#15181c"); // visor slit
    px(g, 6 + bx, 1, 4, 1, "#c3c9d1");
  } else {
    px(g, 6 + bx, 2, 4, 3, pose === "pain" ? "#d98a6a" : pal.skin);
    px(g, 7 + bx, 3, 1, 1, "#1a1a1a"); // eyes
    px(g, 9 + bx, 3, 1, 1, "#1a1a1a");
    if (pal.style === "kettle") {
      px(g, 6 + bx, 0, 4, 2, pal.helmet);
      px(g, 5 + bx, 1.5, 6, 1, shadeColor(pal.helmet.startsWith("#") ? pal.helmet : "#8d949c", 0.75)); // brim
    } else {
      // plume
      px(g, 6 + bx, 1, 4, 2, pal.helmet);
      px(g, 5 + bx, 2, 1, 1, pal.helmet);
      px(g, 10 + bx, 2, 1, 1, pal.helmet);
      px(g, 10 + bx, 0, 1, 2, "#c03030");
      px(g, 11 + bx, 1, 1, 2, "#8c1a1a");
    }
  }

  // arms + crossbow
  if (pose === "aim" || pose === "fire") {
    // both arms forward, crossbow levelled at the viewer
    px(g, 4, 7, 2, 2, pal.uniform);
    px(g, 10, 7, 2, 2, pal.uniform);
    px(g, 5, 8, 6, 1, pal.style === "greathelm" ? pal.helmet : pal.skin);
    px(g, 4, 7, 8, 1, "#4a3018"); // bow limbs
    px(g, 3, 7, 1, 1, "#33220f");
    px(g, 12, 7, 1, 1, "#33220f");
    px(g, 7, 7.5, 2, 2, "#2f2013"); // stock head-on
    if (pose === "fire") {
      px(g, 7, 5.5, 2, 1.5, "#fff6b0"); // loosed bolt
      px(g, 7.5, 4, 1, 1.5, "#ffd23c");
    }
  } else {
    px(g, 4 + bx, 6, 1, 4, pal.uniform);
    px(g, 11 + bx, 6, 1, 4, pal.uniform);
    px(g, 4 + bx, 10, 1, 1, pal.style === "greathelm" ? pal.helmet : pal.skin);
    px(g, 11 + bx, 10, 1, 1, pal.style === "greathelm" ? pal.helmet : pal.skin);
    // crossbow carried at the side
    px(g, 3 + bx, 9, 4, 1, "#4a3018");
    px(g, 2 + bx, 8, 1, 3, "#7c828e");
  }
  return c;
}

// A swarm of bats drawn into a single billboard. One little bat is a body
// with red eyes and a pair of membrane wings caught either mid-upstroke or
// mid-downstroke; the swarm poses scatter several of them across the tile.
function miniBat(g, x, y, flap) {
  const wing = "#4c3a28";
  const wingTip = "#33271a";
  const body = "#241a10";
  if (flap === 0) {
    // wings raised
    px(g, x - 2.1, y - 1.1, 1.8, 0.7, wing);
    px(g, x + 0.8, y - 1.1, 1.8, 0.7, wing);
    px(g, x - 2.8, y - 1.9, 1, 0.9, wingTip);
    px(g, x + 2.3, y - 1.9, 1, 0.9, wingTip);
  } else {
    // wings swept down and out
    px(g, x - 2.6, y + 0.1, 2.2, 0.7, wing);
    px(g, x + 0.9, y + 0.1, 2.2, 0.7, wing);
    px(g, x - 3.1, y + 0.7, 1, 0.8, wingTip);
    px(g, x + 2.6, y + 0.7, 1, 0.8, wingTip);
  }
  px(g, x - 0.6, y - 0.7, 1.7, 1.7, body);
  px(g, x - 0.5, y - 1.1, 0.4, 0.5, body); // ears
  px(g, x + 0.6, y - 1.1, 0.4, 0.5, body);
  px(g, x - 0.3, y - 0.4, 0.4, 0.4, "#e04040"); // eyes
  px(g, x + 0.4, y - 0.4, 0.4, 0.4, "#e04040");
}

// Swarm billboard. pose keys match the soldier set so Enemy.sprite() can
// stay generic; aim/fire never trigger for bats and just alias the flaps.
function batSwarmSprite(pose) {
  const c = canvas();
  const g = c.getContext("2d");
  const FLOCK = [[4.5, 4], [10.5, 3], [7.5, 7], [12, 8.5], [4, 9.5], [9, 11]];

  if (pose === "stand") {
    // roosting: a huddle of bats hanging under the ceiling, eyes aglow
    px(g, 4, 0, 8.5, 2.6, "#181008");
    for (const [i, hx] of [4.8, 6.8, 8.8, 10.8].entries()) {
      const hy = i % 2 ? 1.4 : 0.8;
      px(g, hx - 0.4, hy, 1.9, 1.2, "#241a10"); // folded wings
      px(g, hx, hy, 1.1, 2.4, "#2e2214"); // hanging body
      px(g, hx + 0.05, hy + 2.4, 1, 0.9, "#241a10"); // head at the bottom
      px(g, hx + 0.1, hy + 2.6, 0.35, 0.35, "#e04040");
      px(g, hx + 0.6, hy + 2.6, 0.35, 0.35, "#e04040");
    }
    return c;
  }
  if (pose === "dead") {
    // a few limp bats on the flagstones
    for (const [dx, wsp] of [[4, 2.2], [8.5, 1.8], [11.5, 2]]) {
      px(g, dx - wsp / 2, 14.9, wsp * 2, 0.6, "#241a10");
      px(g, dx - 0.4, 14.5, 1.2, 0.9, "#181008");
    }
    return c;
  }
  if (pose === "die1" || pose === "die2") {
    // the swarm tumbles: wings folded, dropping toward the floor
    const drop = pose === "die1" ? 3 : 6.5;
    for (const [i, [bx, by]] of FLOCK.entries()) {
      const y = Math.min(14, by + drop + (i % 3));
      px(g, bx - 1.3, y, 2.6, 0.8, "#33271a");
      px(g, bx - 0.5, y - 0.5, 1.2, 1.4, "#241a10");
    }
    return c;
  }

  const spread = pose === "pain" ? 1.35 : 1;
  const alt = pose === "walk2" || pose === "fire";
  for (const [i, [bx, by]] of FLOCK.entries()) {
    const x = 8 + (bx - 8) * spread;
    const y = 7.5 + (by - 7.5) * spread + (alt ? (i % 2 ? 0.7 : -0.5) : 0);
    miniBat(g, x, y, (i + (alt ? 1 : 0)) % 2);
  }
  return c;
}

// War hound billboards. Unlike the soldiery (always drawn face-on), the
// hound reads wrong without a heading: stand/walk poses therefore exist in
// four views — front, back and both profiles — and Enemy.sprite() picks one
// from the dog's heading in camera space, the way the arrows do. Pose keys
// still match the soldier set: "fire" doubles as the bite (jaws head-on at
// the viewer), "aim" is never entered by the dog AI. dogSprite() draws the
// right-facing profile plus the shared pain/death poses.
function dogSprite(pose) {
  const c = canvas();
  const g = c.getContext("2d");
  const fur = "#5d4326";
  const dark = "#3b2a16";
  const belly = "#7d5f3a";
  const tongue = "#c04040";

  if (pose === "dead" || pose === "die2") {
    // stretched out on its side
    px(g, 3, 13.6, 10, 1.6, fur);
    px(g, 1.6, 13.9, 1.8, 1.3, dark); // head flat on the flagstones
    px(g, 13, 13.2, 2.2, 0.7, dark); // tail
    px(g, 4.5, 15.1, 6.5, 0.9, dark); // legs folded together
    if (pose === "dead") px(g, 5, 15.5, 5.5, 0.5, "#5c0f0f");
    return c;
  }
  if (pose === "die1") {
    // legs buckling, muzzle dropping
    px(g, 3.5, 12, 8.5, 2.6, fur);
    px(g, 2, 12.8, 2.2, 2.2, dark); // head sagging
    px(g, 5, 14.4, 1.3, 1.6, dark);
    px(g, 9.5, 14.4, 1.3, 1.6, dark);
    px(g, 12, 11.6, 2, 0.8, dark); // tail down
    return c;
  }

  if (pose === "fire") {
    // the bite, head-on: ears flat, jaws wide, teeth bared
    px(g, 5, 6, 6, 5, fur); // head
    px(g, 3.6, 5.4, 1.6, 1.8, dark); // ears swept flat
    px(g, 10.8, 5.4, 1.6, 1.8, dark);
    px(g, 6.1, 7.4, 1, 1, "#e8d84a"); // eyes ablaze
    px(g, 8.9, 7.4, 1, 1, "#e8d84a");
    px(g, 5.4, 10.4, 5.2, 3.6, dark); // open maw
    px(g, 5.9, 10.7, 4.2, 1.2, "#7c1616"); // gullet
    px(g, 5.9, 11.9, 4.2, 1.6, tongue);
    for (let i = 0; i < 4; i++) {
      px(g, 5.9 + i * 1.15, 10.4, 0.6, 0.9, "#e8e4d8"); // fangs top & bottom
      px(g, 5.9 + i * 1.15, 13.2, 0.6, 0.8, "#e8e4d8");
    }
    px(g, 4.2, 13.8, 7.6, 1.6, fur); // shoulders below
    px(g, 4.6, 15.2, 2, 0.8, dark); // forepaws
    px(g, 9.4, 15.2, 2, 0.8, dark);
    return c;
  }

  // side profile: stand / walk1 / walk2 / pain / aim
  const y0 = pose === "pain" ? 10.8 : 10; // back line; pain cowers lower
  px(g, 2, y0 - 0.6, 1.4, 0.8, dark); // tail raised
  px(g, 2.7, y0 + 0.2, 1, 0.9, dark);
  px(g, 3.4, y0, 8, 3, fur); // body
  px(g, 4, y0 + 2.3, 7, 0.9, belly);
  px(g, 10.8, y0 - 2, 2.6, 2.9, fur); // head
  px(g, 13.2, y0 - 1.3, 2, 1.2, fur); // muzzle
  px(g, 14.9, y0 - 1.3, 0.6, 0.7, "#16120c"); // nose
  px(g, 10.9, y0 - 2.8, 0.9, 1.1, dark); // ear pricked up
  px(g, 12.3, y0 - 1.6, 0.6, 0.6, "#141414"); // eye
  if (pose === "pain") px(g, 13.4, y0 - 0.1, 1.4, 0.6, tongue); // yelping

  if (pose === "walk1") {
    // full gallop stretch: hind legs swept back, forelegs reaching
    px(g, 2.4, y0 + 2.9, 1.7, 1.2, fur);
    px(g, 1.4, y0 + 3.7, 1.2, 1, dark);
    px(g, 11.2, y0 + 2.9, 1.7, 1.2, fur);
    px(g, 12.6, y0 + 3.7, 1.2, 1, dark);
  } else if (pose === "walk2") {
    // legs gathered under the body
    px(g, 5.2, y0 + 2.8, 1.3, 2.7, fur);
    px(g, 8.8, y0 + 2.8, 1.3, 2.7, fur);
    px(g, 5, y0 + 5, 1.6, 0.7, dark);
    px(g, 8.6, y0 + 5, 1.6, 0.7, dark);
  } else {
    // standing square on all fours
    px(g, 4.2, y0 + 3, 1.1, 2.8, fur);
    px(g, 5.9, y0 + 3.2, 1, 2.6, dark);
    px(g, 9.2, y0 + 3, 1.1, 2.8, fur);
    px(g, 10.9, y0 + 3.2, 1, 2.6, dark);
  }
  return c;
}

// The hound head-on: pricked ears, chest between the forelegs. Gallop
// frames alternate the legs and bob the whole body.
function dogFrontSprite(pose) {
  const c = canvas();
  const g = c.getContext("2d");
  const fur = "#5d4326";
  const dark = "#3b2a16";
  const belly = "#7d5f3a";
  const bob = pose === "walk2" ? 0.4 : 0;

  px(g, 4.6, 11 + bob, 1, 2.6, dark); // haunches peeking past the shoulders
  px(g, 10.4, 11 + bob, 1, 2.6, dark);
  px(g, 5.4, 9.8 + bob, 5.2, 4, fur); // chest & shoulders
  px(g, 6.6, 11.6 + bob, 2.8, 2.2, belly); // chest blaze
  px(g, 5.8, 5.6 + bob, 1, 1.5, dark); // pricked ears
  px(g, 9.2, 5.6 + bob, 1, 1.5, dark);
  px(g, 6, 6.6 + bob, 4, 3.2, fur); // head
  px(g, 6.7, 7.5 + bob, 0.8, 0.8, "#141414"); // eyes
  px(g, 8.5, 7.5 + bob, 0.8, 0.8, "#141414");
  px(g, 7.1, 8.5 + bob, 1.8, 1.5, dark); // muzzle
  px(g, 7.5, 8.5 + bob, 1, 0.7, "#16120c"); // nose

  if (pose === "walk1") {
    px(g, 5.8, 13.4, 1.3, 2.5, fur); // near leg planted
    px(g, 8.9, 13.4, 1.3, 1.7, dark); // off leg lifted
  } else if (pose === "walk2") {
    px(g, 8.9, 13.4, 1.3, 2.5, fur);
    px(g, 5.8, 13.4, 1.3, 1.7, dark);
  } else {
    px(g, 5.8, 13.6, 1.3, 2.2, fur);
    px(g, 8.9, 13.6, 1.3, 2.2, fur);
    px(g, 5.6, 15.3, 1.6, 0.6, dark); // paws
    px(g, 8.7, 15.3, 1.6, 0.6, dark);
  }
  return c;
}

// The hound from behind: rump and hind legs, ears past the head, the tail
// in front of everything and swaying with the gallop.
function dogBackSprite(pose) {
  const c = canvas();
  const g = c.getContext("2d");
  const fur = "#5d4326";
  const dark = "#3b2a16";
  const bob = pose === "walk2" ? 0.4 : 0;
  const sway = pose === "walk1" ? -0.5 : pose === "walk2" ? 0.5 : 0;

  px(g, 6, 5.8 + bob, 1, 1.4, dark); // ears from behind
  px(g, 8.8, 5.8 + bob, 1, 1.4, dark);
  px(g, 6.2, 6.8 + bob, 3.6, 3.1, fur); // back of the head, no face
  px(g, 5.2, 9.8 + bob, 5.6, 4, fur); // rump
  px(g, 5.6, 12.4 + bob, 4.8, 1.4, dark); // shadow under it

  if (pose === "walk1") {
    px(g, 5.7, 13.6, 1.3, 2.3, fur);
    px(g, 8.8, 13.6, 1.3, 1.6, dark); // off leg tucked
  } else if (pose === "walk2") {
    px(g, 8.8, 13.6, 1.3, 2.3, fur);
    px(g, 5.7, 13.6, 1.3, 1.6, dark);
  } else {
    px(g, 5.7, 13.8, 1.3, 2.1, fur);
    px(g, 8.8, 13.8, 1.3, 2.1, fur);
    px(g, 5.5, 15.3, 1.6, 0.6, dark);
    px(g, 8.6, 15.3, 1.6, 0.6, dark);
  }
  // tail last: nearest the viewer, raised over the rump
  px(g, 8.3 + sway, 7.4 + bob, 0.9, 2.8, dark);
  px(g, 8.1 + sway, 6.7 + bob, 1.3, 0.9, dark);
  return c;
}

// Right-facing profiles flipped for the left view.
function mirrorSprite(src) {
  const c = canvas();
  const g = c.getContext("2d");
  g.translate(64, 0);
  g.scale(-1, 1);
  g.drawImage(src, 0, 0);
  return c;
}

const PAL_GUARD = { uniform: "#6e5638", dark: "#463621", trim: "#a3823f", skin: "#dba377", helmet: "#8d949c", style: "kettle" };
const PAL_KNIGHT = { uniform: "#8d959f", dark: "#565e66", trim: "#c9a24a", skin: "#dba377", helmet: "#9aa2ac", style: "greathelm" };
const PAL_CAPTAIN = { uniform: "#8c2020", dark: "#5a1414", trim: "#c9a24a", skin: "#dba377", helmet: "#8d949c", style: "plume" };

function itemSprite(kind) {
  const c = canvas();
  const g = c.getContext("2d");
  switch (kind) {
    case "key":
      px(g, 6, 5, 4, 4, "#e9c93c");
      px(g, 7, 6, 2, 2, "#8f7a1c");
      px(g, 7, 9, 2, 5, "#e9c93c");
      px(g, 9, 11, 2, 1, "#e9c93c");
      px(g, 9, 13, 2, 1, "#e9c93c");
      px(g, 6, 5, 1, 1, "#fff6b0");
      break;
    case "potion":
      px(g, 7, 5, 2, 2, "#8a6a3a"); // cork
      px(g, 7, 7, 2, 1, "#cfe2e8"); // neck
      px(g, 5, 8, 6, 5, "#c02030"); // round body
      px(g, 4, 9, 8, 3, "#c02030");
      px(g, 6, 13, 4, 1, "#8a1622");
      px(g, 6, 9, 1, 2, "#ff9a9a"); // glint
      break;
    case "bread":
      px(g, 4, 9, 8, 4, "#a06a30");
      px(g, 5, 8, 6, 1, "#c98f4e");
      px(g, 4, 13, 8, 1, "#7c4e1e");
      px(g, 6, 9, 1, 2, "#7c4e1e"); // slashes
      px(g, 9, 9, 1, 2, "#7c4e1e");
      break;
    case "bolts":
      // quiver of crossbow bolts
      px(g, 6, 8, 4, 6, "#5a3a1e");
      px(g, 6, 8, 4, 1, "#7a5230");
      px(g, 6, 13, 4, 1, "#3c2612");
      px(g, 6.5, 5, 1, 3, "#8a6a3a"); // shafts
      px(g, 8.5, 4.5, 1, 3.5, "#8a6a3a");
      px(g, 6.5, 4, 1, 1, "#c9d4d8"); // steel tips
      px(g, 8.5, 3.5, 1, 1, "#c9d4d8");
      break;
    case "treasure":
      // golden chalice
      px(g, 5, 6, 6, 3, "#e9c93c");
      px(g, 6, 9, 4, 1, "#d0a930");
      px(g, 7, 10, 2, 2, "#d0a930");
      px(g, 5, 12, 6, 1, "#e9c93c");
      px(g, 7, 7, 2, 1, "#c02040"); // gem
      px(g, 5, 6, 2, 1, "#fff6b0");
      break;
    case "arbalest":
      // repeating crossbow lying flat
      px(g, 3, 8, 10, 1, "#4a3018"); // limbs
      px(g, 3, 7, 1, 1, "#33220f");
      px(g, 12, 7, 1, 1, "#33220f");
      px(g, 3, 9, 10, 0.8, "#b8c4c8"); // string
      px(g, 6, 9.8, 4, 2, "#5a3a1e"); // magazine
      px(g, 7, 11.8, 2, 2.5, "#3a2a16"); // stock
      break;
  }
  return c;
}

// Trap paraphernalia: the floor plate (armed / pressed flat) and the arrow
// that flies out of the slit, drawn at chest height. The arrow comes in four
// views picked by its flight direction relative to the camera: side-on
// left/right, point-first (flying at the viewer) and tail-first.
function trapSprite(kind) {
  const c = canvas();
  const g = c.getContext("2d");
  if (kind === "arrow") {
    px(g, 2, 7, 10, 0.8, "#6a4a26"); // shaft
    px(g, 2, 7, 10, 0.3, "#8a6a3a"); // highlight
    px(g, 12, 6.6, 2.2, 1.6, "#c9d4d8"); // steel head
    px(g, 1, 6.2, 1.6, 1, "#8c2020"); // fletching
    px(g, 1, 7.6, 1.6, 1, "#8c2020");
  } else if (kind === "arrowLeft") {
    const right = trapSprite("arrow");
    g.translate(TEX, 0);
    g.scale(-1, 1);
    g.drawImage(right, 0, 0);
  } else if (kind === "arrowToward") {
    // point-first: dark socket, bright steel tip, fletching peeking behind
    px(g, 6.6, 5.9, 0.9, 0.9, "#8c2020");
    px(g, 8.5, 5.9, 0.9, 0.9, "#8c2020");
    px(g, 6.6, 7.4, 0.9, 0.9, "#8c2020");
    px(g, 8.5, 7.4, 0.9, 0.9, "#8c2020");
    px(g, 7, 6.4, 2, 1.6, "#5c666c"); // socket ring
    px(g, 7.4, 6.7, 1.2, 1, "#e8f0f4"); // gleaming point
  } else if (kind === "arrowAway") {
    // tail-first: red fletching cross around the shaft's end
    px(g, 6, 6.9, 4, 1.2, "#8c2020");
    px(g, 7.4, 5.5, 1.2, 4, "#8c2020");
    px(g, 7.4, 6.9, 1.2, 1.2, "#3a2a16"); // shaft butt
  } else {
    const pressed = kind === "pressed";
    px(g, 3, pressed ? 14.2 : 13.2, 10, pressed ? 0.8 : 1.8, pressed ? "#57503f" : "#726a52");
    px(g, 3, 15, 10, 0.8, pressed ? "#3e382c" : "#514a39"); // front edge
    px(g, 3, 15.7, 10, 0.3, "#181209"); // ground shadow
    // iron studs at the corners
    g.fillStyle = "#23262b";
    for (const sx of [14, 48]) g.fillRect(sx, (pressed ? 14.4 : 13.6) * 4, 3, 3);
  }
  return c;
}

// -------------------------------------------------------- weapon view (POV)

// Weapons are modelled as small assemblies of 3D boxes and rendered with a
// perspective camera sitting at the player's eye: flat-shaded faces sorted
// back-to-front, strings and bolt streaks drawn as projected 3D lines.
//
// Camera space: x right, y up, z forward into the screen; the eye is at the
// origin. Each part is an axis-aligned box (optionally rotated around its
// own center, optionally tapered toward one end) with a base color.

const WV = 192; // weapon canvas size
const W_FOCAL = 150;
const W_CX = WV / 2;
const W_CY = 78;

const v3sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const v3dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const v3cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
const v3norm = (v) => {
  const l = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / l, v[1] / l, v[2] / l];
};

const W_LIGHT = v3norm([-0.4, 0.75, -0.5]); // torch above the left shoulder

function rot3(v, r) {
  let [x, y, z] = v;
  if (r[0]) {
    const c = Math.cos(r[0]), s = Math.sin(r[0]);
    [y, z] = [y * c - z * s, y * s + z * c];
  }
  if (r[1]) {
    const c = Math.cos(r[1]), s = Math.sin(r[1]);
    [x, z] = [x * c + z * s, -x * s + z * c];
  }
  if (r[2]) {
    const c = Math.cos(r[2]), s = Math.sin(r[2]);
    [x, y] = [x * c - y * s, x * s + y * c];
  }
  return [x, y, z];
}

const wProject = (v) => [W_CX + (W_FOCAL * v[0]) / v[2], W_CY - (W_FOCAL * v[1]) / v[2]];

// quad corner indices of a box whose vertex i has bits (x:4, y:2, z:1)
const BOX_FACES = [
  [0, 1, 3, 2], [4, 5, 7, 6], // -x +x
  [0, 1, 5, 4], [2, 3, 7, 6], // -y +y
  [0, 2, 6, 4], [1, 3, 7, 5], // -z +z
];

// part: { p:[x,y,z] center, s:[w,h,d], c:"#hex", r:[rx,ry,rz]?, t:{axis,k,end}? }
// taper t scales the cross-section at one end of the box (end: +1 or -1).
function collectFaces(part, out, world) {
  const [hw, hh, hd] = [part.s[0] / 2, part.s[1] / 2, part.s[2] / 2];
  const verts = [];
  for (let i = 0; i < 8; i++) {
    let v = [i & 4 ? hw : -hw, i & 2 ? hh : -hh, i & 1 ? hd : -hd];
    const t = part.t;
    if (t && Math.sign(v[t.axis]) === (t.end ?? 1)) {
      for (let a = 0; a < 3; a++) if (a !== t.axis) v[a] *= t.k;
    }
    if (part.r) v = rot3(v, part.r);
    verts.push(world([v[0] + part.p[0], v[1] + part.p[1], v[2] + part.p[2]]));
  }
  const pc = world(part.p);
  for (const f of BOX_FACES) {
    const q = f.map((i) => verts[i]);
    const cen = [
      (q[0][0] + q[1][0] + q[2][0] + q[3][0]) / 4,
      (q[0][1] + q[1][1] + q[2][1] + q[3][1]) / 4,
      (q[0][2] + q[1][2] + q[2][2] + q[3][2]) / 4,
    ];
    let n = v3norm(v3cross(v3sub(q[1], q[0]), v3sub(q[2], q[0])));
    if (v3dot(n, v3sub(cen, pc)) < 0) n = [-n[0], -n[1], -n[2]]; // outward
    if (v3dot(n, cen) >= 0) continue; // facing away from the eye
    const b = 0.38 + 0.62 * Math.max(0, v3dot(n, W_LIGHT));
    out.push({ z: cen[2], pts: q.map(wProject), c: part.c, b });
  }
}

// Rotate a finished model around the vertical axis through `pivot`, so the
// weapon is seen slightly from the side — pure head-on views read as flat.
function yawModel(model, yaw, pivot) {
  const cy = Math.cos(yaw), sy = Math.sin(yaw);
  const R = (v) => {
    const x = v[0] - pivot[0], z = v[2] - pivot[2];
    return [pivot[0] + x * cy + z * sy, v[1], pivot[2] - x * sy + z * cy];
  };
  for (const p of model.parts) {
    p.p = R(p.p);
    p.r = p.r ? [p.r[0], p.r[1] + yaw, p.r[2]] : [0, yaw, 0];
  }
  for (const l of model.lines) {
    l.a = R(l.a);
    l.b = R(l.b);
  }
}

function renderWeaponModel(g, model) {
  const { parts, lines, pitch = 0, pivot = [0, 0, 0.5] } = model;
  // global pitch: tilt the whole weapon nose-down so its top faces show
  const pc = Math.cos(pitch), ps = Math.sin(pitch);
  const world = (v) => {
    const y = v[1] - pivot[1], z = v[2] - pivot[2];
    return [v[0], pivot[1] + y * pc - z * ps, pivot[2] + y * ps + z * pc];
  };
  const faces = [];
  for (const p of parts) collectFaces(p, faces, world);
  faces.sort((a, b) => b.z - a.z); // painter's algorithm: far first
  for (const f of faces) {
    g.fillStyle = shadeColor(f.c, f.b);
    g.strokeStyle = shadeColor(f.c, f.b * 0.8); // darker edges define the form
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(f.pts[0][0], f.pts[0][1]);
    for (let i = 1; i < 4; i++) g.lineTo(f.pts[i][0], f.pts[i][1]);
    g.closePath();
    g.fill();
    g.stroke();
  }
  for (const ln of lines) {
    const a = wProject(world(ln.a));
    const b = wProject(world(ln.b));
    g.strokeStyle = ln.c;
    g.lineWidth = ln.w ?? 2;
    g.beginPath();
    g.moveTo(a[0], a[1]);
    g.lineTo(b[0], b[1]);
    g.stroke();
  }
}

const W_SKIN = "#dba377";
const W_SKIN_D = "#c08b5c";
const W_WOOD = "#5a3a20";
const W_WOOD_D = "#35240f";
const W_LIMB = "#523823";
const W_GOLD = "#c9a24a";
const W_STEEL = "#c4ced4";
const W_STRING = "#c9bd9c";

const W_LEATHER = "#4a3220";

function weaponModel(kind, frame) {
  const parts = [];
  const lines = [];
  const P = (p, s, c, r, t) => parts.push({ p, s, c, r, t });
  const model = { parts, lines };

  if (kind === "dagger") {
    // held point-forward: the blade juts out of the fist toward the
    // crosshair, and the stab thrusts the whole arm deeper into the scene
    const k = frame === 1 ? 1 : frame === 2 ? 0.45 : 0;
    const at = (x, y, z) => [x, y + 0.02 * k, z + 0.24 * k];
    P(at(0.1, -0.38, 0.44), [0.14, 0.3, 0.13], W_SKIN, [0.35, 0, 0.12]); // forearm
    P(at(0.11, -0.48, 0.4), [0.16, 0.14, 0.15], W_LEATHER, [0.35, 0, 0.12]); // bracer
    P(at(0.06, -0.24, 0.5), [0.16, 0.13, 0.16], W_SKIN, [0.05, 0.1, 0]); // fist
    P(at(-0.02, -0.21, 0.455), [0.05, 0.05, 0.07], W_SKIN_D); // thumb
    P(at(0.06, -0.21, 0.415), [0.05, 0.06, 0.035], W_GOLD); // pommel behind the fist
    P(at(0.06, -0.2, 0.575), [0.2, 0.05, 0.035], W_GOLD, [-0.35, 0, 0]); // crossguard
    P(at(0.06, -0.135, 0.745), [0.045, 0.02, 0.36], W_STEEL, [-0.35, 0, 0], { axis: 2, k: 0.15 }); // blade
    yawModel(model, -0.15, [0.06, 0, 0.5]);
    model.pitch = 0.06;
    model.pivot = [0, -0.2, 0.5];
  } else if (kind === "crossbow") {
    const k = frame === 1 ? 1 : frame === 2 ? 0.5 : 0;
    const at = (x, y, z) => [x + 0.03, y - 0.06 + 0.05 * k, z - 0.04 * k]; // sits low; recoil up and back
    P(at(0.02, -0.35, 0.46), [0.14, 0.3, 0.13], W_SKIN, [0.32, 0, 0]); // forearm
    P(at(0.03, -0.46, 0.42), [0.16, 0.14, 0.15], W_LEATHER, [0.32, 0, 0]); // bracer
    P(at(0, -0.21, 0.52), [0.16, 0.12, 0.15], W_SKIN); // fist on the grip
    P(at(0, -0.12, 0.4), [0.09, 0.13, 0.14], "#4a3018"); // butt at the cheek
    P(at(0, -0.1, 0.6), [0.08, 0.08, 0.42], "#6a4426", null, { axis: 2, k: 0.75 }); // tiller
    // wooden limbs swept back toward the shooter, steel lath on their face
    P(at(-0.19, -0.045, 0.72), [0.38, 0.06, 0.065], W_LIMB, [0, -0.4, -0.18], { axis: 0, end: -1, k: 0.5 });
    P(at(0.19, -0.045, 0.72), [0.38, 0.06, 0.065], W_LIMB, [0, 0.4, 0.18], { axis: 0, end: 1, k: 0.5 });
    const tipL = at(-0.36, -0.011, 0.646);
    const tipR = at(0.36, -0.011, 0.646);
    if (frame === 0) {
      // string cocked back to the nut, bolt lying on the tiller
      const nut = at(0, -0.065, 0.5);
      lines.push({ a: tipL, b: nut, c: W_STRING, w: 2 }, { a: tipR, b: nut, c: W_STRING, w: 2 });
      P(at(0, -0.03, 0.62), [0.024, 0.024, 0.36], "#5d3f1c", null, { axis: 2, k: 0.5 }); // shaft
      P(at(0, -0.03, 0.82), [0.045, 0.045, 0.08], W_STEEL); // head
      P(at(0, -0.025, 0.49), [0.018, 0.065, 0.08], "#a03030"); // fletching
      P(at(0, -0.025, 0.49), [0.065, 0.018, 0.08], "#a03030");
    } else {
      // loosed: string resting against the front of the lath
      const front = at(0, -0.055, 0.68);
      lines.push({ a: tipL, b: front, c: W_STRING, w: 2 }, { a: tipR, b: front, c: W_STRING, w: 2 });
      if (frame === 1) {
        lines.push({ a: [0, 0.03, 1.2], b: [0, 0.1, 2.4], c: "#f2ecd8", w: 2 }); // bolt racing away
      }
    }
    yawModel(model, 0.08, [0.03, 0, 0.55]);
    model.pitch = 0.16;
    model.pivot = [0, -0.12, 0.5];
  } else if (kind === "arbalest") {
    const k = frame === 1 ? 1 : frame === 2 ? 0.5 : 0;
    const at = (x, y, z) => [x + 0.02, y - 0.06 + 0.04 * k, z - 0.04 * k]; // sits low
    // both arms braced
    P(at(-0.21, -0.38, 0.45), [0.11, 0.28, 0.1], W_SKIN, [0.28, 0, -0.18]);
    P(at(0.21, -0.38, 0.45), [0.11, 0.28, 0.1], W_SKIN, [0.28, 0, 0.18]);
    P(at(-0.24, -0.47, 0.42), [0.13, 0.12, 0.12], W_LEATHER, [0.28, 0, -0.18]); // bracers
    P(at(0.24, -0.47, 0.42), [0.13, 0.12, 0.12], W_LEATHER, [0.28, 0, 0.18]);
    P(at(-0.16, -0.24, 0.51), [0.13, 0.1, 0.13], W_SKIN); // fists
    P(at(0.16, -0.24, 0.51), [0.13, 0.1, 0.13], W_SKIN);
    P(at(0, -0.13, 0.38), [0.11, 0.15, 0.16], "#4a3018"); // butt
    P(at(0, -0.09, 0.6), [0.11, 0.1, 0.46], "#6a4426", null, { axis: 2, k: 0.8 }); // tiller
    // magazine of the repeating mechanism riding on top, iron-strapped
    P(at(0, 0.015, 0.58), [0.12, 0.1, 0.22], "#7a5230");
    P(at(0, 0.015, 0.51), [0.13, 0.11, 0.02], "#2e2012");
    P(at(0, 0.015, 0.65), [0.13, 0.11, 0.02], "#2e2012");
    // heavy limbs with steel laths
    P(at(-0.2, -0.04, 0.74), [0.4, 0.065, 0.075], W_LIMB, [0, -0.45, -0.2], { axis: 0, end: -1, k: 0.5 });
    P(at(0.2, -0.04, 0.74), [0.4, 0.065, 0.075], W_LIMB, [0, 0.45, 0.2], { axis: 0, end: 1, k: 0.5 });
    const tipL = at(-0.375, 0.0, 0.666);
    const tipR = at(0.375, 0.0, 0.666);
    const hook = at(0, -0.07, 0.52);
    lines.push({ a: tipL, b: hook, c: W_STRING, w: 2 }, { a: tipR, b: hook, c: W_STRING, w: 2 });
    if (frame === 1) {
      lines.push({ a: [0, 0.04, 1.2], b: [0, 0.12, 2.4], c: "#f2ecd8", w: 3 }); // bolt racing away
    }
    yawModel(model, 0.08, [0.02, 0, 0.55]);
    model.pitch = 0.16;
    model.pivot = [0, -0.12, 0.5];
  }
  return model;
}

function weaponView(kind, frame) {
  const c = canvas(WV, WV);
  const g = c.getContext("2d");
  renderWeaponModel(g, weaponModel(kind, frame));
  return c;
}

// Flat pixel-art emblems for the HUD "ARMS" panel — the 3D POV renders make
// poor icons, so these stay 2D like the rest of the HUD.
function weaponIcon(kind) {
  const c = canvas(64, 64);
  const g = c.getContext("2d");
  const P = (x, y, w, h, col) => {
    g.fillStyle = col;
    g.fillRect(x * 4, y * 4, w * 4, h * 4);
  };
  if (kind === "dagger") {
    P(7.2, 0.6, 1.6, 1, "#eef4f6"); // tip
    P(7.2, 1.4, 1.6, 5.8, "#c9d4d8"); // blade
    P(7.2, 1.4, 0.6, 5.8, "#eef4f6");
    P(5, 7.2, 6, 1.3, "#c9a24a"); // guard
    P(6.9, 8.5, 2.2, 4.2, "#3a2a16"); // wrapped grip
    P(6.9, 9.6, 2.2, 0.6, "#5a4326");
    P(6.9, 11, 2.2, 0.6, "#5a4326");
    P(6.6, 12.7, 2.8, 1.5, "#c9a24a"); // pommel
  } else if (kind === "crossbow") {
    P(1.5, 3.4, 13, 1.6, "#3c2818"); // bow
    P(0.8, 2.6, 1.4, 1.8, "#2c1c10"); // upswept tips
    P(13.8, 2.6, 1.4, 1.8, "#2c1c10");
    P(1.5, 3.4, 13, 0.5, "#9aa2ac"); // steel lath
    const s = "#c9bd9c"; // string drawn back to the nut
    P(1.2, 4.6, 2.4, 0.5, s);
    P(3.6, 5.6, 2.2, 0.5, s);
    P(5.8, 6.6, 1.8, 0.5, s);
    P(12.4, 4.6, 2.4, 0.5, s);
    P(10.2, 5.6, 2.2, 0.5, s);
    P(8.4, 6.6, 1.8, 0.5, s);
    P(7.2, 3, 1.6, 9.5, "#5a3a20"); // stock
    P(7.2, 3, 0.6, 9.5, "#6f4a28");
    P(6.8, 12.5, 2.4, 1.5, "#4a3018"); // butt
  } else {
    P(1, 4.2, 14, 1.9, "#3c2818"); // heavier bow
    P(0.3, 3.3, 1.5, 2, "#2c1c10");
    P(14.2, 3.3, 1.5, 2, "#2c1c10");
    P(1, 4.2, 14, 0.5, "#9aa2ac");
    const s = "#c9bd9c";
    P(0.8, 5.7, 2.6, 0.5, s);
    P(3.4, 6.7, 2.4, 0.5, s);
    P(5.8, 7.7, 2, 0.5, s);
    P(12.6, 5.7, 2.6, 0.5, s);
    P(10.2, 6.7, 2.4, 0.5, s);
    P(8.2, 7.7, 2, 0.5, s);
    P(6, 1, 4, 3.2, "#7a5230"); // magazine above the bow
    P(6, 1, 4, 0.7, "#8d6238");
    P(6.9, 4.2, 2.2, 8.3, "#5a3a20"); // stock
    P(6.9, 4.2, 0.7, 8.3, "#6f4a28");
    P(6.4, 12.5, 3.2, 1.8, "#4a3018"); // butt
  }
  return c;
}

// A compact wall-mounted torch. Four slightly different silhouettes are
// cycled by the renderer; the chunky shapes preserve the game's pixel-art
// style even when the sprite is scaled up at close range.
function torchSprite(frame) {
  const c = canvas();
  const g = c.getContext("2d");
  const sway = [-2, 1, 2, -1][frame];
  const tip = [1, -2, 0, -1][frame];

  // iron wall bracket and its two studs
  g.fillStyle = "rgba(0,0,0,0.45)";
  g.fillRect(26, 33, 17, 5);
  g.fillRect(39, 27, 5, 14);
  g.fillStyle = "#34373a";
  g.fillRect(25, 31, 17, 4);
  g.fillRect(39, 27, 4, 12);
  g.fillStyle = "#858078";
  g.fillRect(40, 28, 1, 10);
  g.fillRect(40, 29, 2, 2);
  g.fillRect(40, 35, 2, 2);

  // tarred wooden shaft and wrapped grip below the flame
  g.fillStyle = "#25160d";
  g.fillRect(27, 28, 8, 29);
  g.fillStyle = "#68401f";
  g.fillRect(28, 29, 5, 27);
  g.fillStyle = "#9a6630";
  g.fillRect(29, 30, 2, 25);
  g.fillStyle = "#332014";
  for (const y of [31, 35, 39]) g.fillRect(26, y, 10, 2);

  // outer red flame, orange body and a hot yellow-white core
  g.fillStyle = "rgba(120,25,4,0.65)";
  g.fillRect(22 + sway, 9 + tip, 18, 20 - tip);
  g.fillRect(25 + sway, 5 + tip, 12, 8);
  g.fillRect(28 + sway, 2 + tip, 6, 6);
  g.fillStyle = "#e94b0c";
  g.fillRect(24 + sway, 10 + tip, 14, 17 - tip);
  g.fillRect(27 + sway, 6 + tip, 9, 10);
  g.fillStyle = "#ff9f16";
  g.fillRect(27 + sway, 13 + tip, 9, 13 - tip);
  g.fillRect(29 + sway, 9 + tip, 5, 8);
  g.fillStyle = "#ffe36a";
  g.fillRect(29 + sway, 17 + tip, 5, 8 - tip);
  g.fillStyle = "#fff3b0";
  g.fillRect(30 + sway, 20 + tip, 3, 4);
  return c;
}

// Courtyard fountain: an octagonal stone basin with a carved pedestal and an
// upper bowl. The jet and the spill animate over four frames like the
// torches, so the water never stands still.
function fountainSprite(frame) {
  const c = canvas();
  const g = c.getContext("2d");

  // pool water first — the stone rim then overlaps its front edge
  px(g, 2.4, 10.4, 11.2, 1.4, "#2e5a7a");
  const ring = [4, 6, 8, 10][frame];
  px(g, 8 - ring / 2, 10.7, ring, 0.35, "#4a86b0"); // ripple drifting outward
  px(g, 6.4, 10.45, 3.2, 0.3, "#9fd4ea"); // churn where the spill lands

  // octagonal basin
  px(g, 1.6, 11.4, 12.8, 3.4, "#5c554b");
  px(g, 2.4, 14.8, 11.2, 0.7, "#37322b"); // plinth in shadow
  px(g, 1.6, 11.4, 12.8, 0.5, "#8a8074"); // lit rim
  px(g, 1.6, 11.4, 0.7, 3.4, "#6b6157"); // corner catching the light
  px(g, 13.7, 11.4, 0.7, 3.4, "#443e36");
  for (const x of [4.8, 8, 11.2]) px(g, x, 12.4, 0.35, 2.4, "#443e36"); // panel joints

  // pedestal and upper bowl, brimming over
  px(g, 7, 6.2, 2, 4.6, "#5c554b");
  px(g, 7, 6.2, 0.6, 4.6, "#7a7268");
  px(g, 4.9, 5.2, 6.2, 1.2, "#6b6157");
  px(g, 4.9, 5.2, 6.2, 0.4, "#8a8074");
  px(g, 5.6, 4.9, 4.8, 0.5, "#3a6f94");

  // the jet rises, feathers at the tip and falls back into the bowl
  const rise = [0, -0.5, -0.9, -0.5][frame];
  const sway = [0, 0.3, 0, -0.3][frame];
  px(g, 7.6 + sway, 2.2 + rise, 0.8, 2.9 - rise, "#4a86b0");
  px(g, 7.8 + sway, 1.6 + rise, 0.4, 1.4, "#9fd4ea");
  px(g, 7.9, 4.6, 0.35, 0.7, "#9fd4ea"); // splash where it lands

  // twin streams spilling from the bowl, broken into falling drops
  for (const sx of [5.3, 10.4]) {
    for (let i = 0; i < 4; i++) {
      const y = 6.6 + i * 1.1 + ((frame + i) % 2) * 0.45;
      px(g, sx, y, 0.4, 0.8, i % 2 === (frame & 1) ? "#9fd4ea" : "#4a86b0");
    }
  }
  return c;
}

// Furniture for the larger halls: a trestle table set with a candle and a
// pewter mug, and a plain oak chair. Both stand on the floor line so the
// renderer can drop them in like any other ground sprite.
function tableSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  // trestle legs with a lit edge, tied by a stretcher beam
  px(g, 2.2, 9.2, 1.6, 6.8, "#33220f");
  px(g, 12.2, 9.2, 1.6, 6.8, "#33220f");
  px(g, 2.2, 9.2, 0.6, 6.8, "#5a3a20");
  px(g, 12.2, 9.2, 0.6, 6.8, "#5a3a20");
  px(g, 3.8, 12.6, 8.4, 1.1, "#2a1c0c");
  // thick oak slab, top catching the torchlight
  px(g, 0.7, 7.4, 14.6, 2, "#6a4426");
  px(g, 0.7, 7.4, 14.6, 0.7, "#8a5c30");
  px(g, 0.7, 9, 14.6, 0.4, "#241708"); // shadow under the lip
  // candle in an iron holder
  px(g, 4.2, 7, 2, 0.5, "#2f3338");
  px(g, 4.7, 5.2, 1, 1.9, "#e8e0c8");
  px(g, 4.95, 4.3, 0.5, 0.9, "#ffd23c");
  px(g, 5.05, 4, 0.3, 0.5, "#fff6b0");
  // pewter mug
  px(g, 9.4, 5.9, 2, 1.7, "#565c66");
  px(g, 9.4, 5.9, 2, 0.4, "#8d949c");
  px(g, 11.4, 6.3, 0.6, 1, "#454b52");
  return c;
}

function chairSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  // stiles: back legs running up into the backrest
  px(g, 4.6, 2.6, 1.2, 13.4, "#4a3018");
  px(g, 10.2, 2.6, 1.2, 13.4, "#4a3018");
  px(g, 4.6, 2.6, 0.4, 13.4, "#6a4426");
  // backrest slats
  px(g, 5.8, 3.4, 4.4, 1.1, "#5a3a20");
  px(g, 5.8, 5.6, 4.4, 1.1, "#5a3a20");
  // seat board
  px(g, 3.8, 8.6, 8.4, 1.6, "#6a4426");
  px(g, 3.8, 8.6, 8.4, 0.6, "#8a5c30");
  // front legs
  px(g, 4, 10.2, 1, 5.8, "#33220f");
  px(g, 11, 10.2, 1, 5.8, "#33220f");
  return c;
}

// Oak wardrobe faces, drawn by the renderer as a shallow box standing flush
// against a wall (front face plus two short sides — never a billboard).
// The renderer fogs the whole column rect, so both textures must be fully
// opaque edge to edge.
function wardrobeFrontTexture() {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(97531);
  px(g, 0, 0, 16, 16, "#3a2612"); // carcass
  // cornice and plinth
  px(g, 0, 0, 16, 1.4, "#241708");
  px(g, 0.3, 0.2, 15.4, 0.4, "#6a4426");
  px(g, 0, 14.6, 16, 1.4, "#241708");
  px(g, 0, 14.6, 16, 0.3, "#0f0903");
  // door leaves with recessed panels
  const door = (x) => {
    px(g, x, 1.6, 6.6, 13, "#5a3a20");
    px(g, x, 1.6, 6.6, 0.4, "#8a5c30"); // lit top rail
    px(g, x, 1.6, 0.4, 13, "#6a4426"); // lit outer stile
    px(g, x + 6.2, 1.6, 0.4, 13, "#33220f");
    px(g, x + 1.1, 2.9, 4.4, 4.6, "#3a2814");
    px(g, x + 1.1, 2.9, 4.4, 0.4, "#241708");
    px(g, x + 1.1, 7.2, 4.4, 0.3, "#7c5228");
    px(g, x + 1.1, 8.8, 4.4, 4.6, "#3a2814");
    px(g, x + 1.1, 8.8, 4.4, 0.4, "#241708");
    px(g, x + 1.1, 13.1, 4.4, 0.3, "#7c5228");
  };
  door(0.9);
  door(8.5);
  px(g, 7.7, 1.6, 0.6, 13, "#241708"); // gap between the leaves
  // strap hinges on the outer stiles
  for (const y of [3.4, 11.8]) {
    px(g, 0.9, y, 1.8, 0.7, "#2f3338");
    px(g, 13.3, y, 1.8, 0.7, "#2f3338");
    px(g, 0.9, y, 1.8, 0.25, "#565c66");
    px(g, 13.3, y, 1.8, 0.25, "#565c66");
  }
  // iron pull rings either side of the gap
  px(g, 6.7, 7.5, 0.8, 1.5, "#2f3338");
  px(g, 8.5, 7.5, 0.8, 1.5, "#2f3338");
  px(g, 6.85, 7.65, 0.5, 0.5, "#8d949c");
  px(g, 8.65, 7.65, 0.5, 0.5, "#8d949c");
  speckle(g, rand, "#000", 70, 0.12);
  speckle(g, rand, "#c98d4c", 18, 0.05);
  return c;
}

function wardrobeSideTexture() {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(24680);
  px(g, 0, 0, 16, 16, "#422b15"); // plain panelled flank
  for (const x of [4, 8, 12]) px(g, x, 0, 0.35, 16, "#2a1c0c"); // plank seams
  px(g, 0, 0, 16, 1.4, "#241708"); // cornice
  px(g, 0.3, 0.2, 15.4, 0.4, "#5a3a20");
  px(g, 0, 14.6, 16, 1.4, "#241708"); // plinth
  // wood grain
  g.globalAlpha = 0.18;
  for (let i = 0; i < 44; i++) {
    g.fillStyle = shadeColor("#6a4426", 0.6 + rand() * 0.7);
    g.fillRect(Math.floor(rand() * TEX), Math.floor(rand() * TEX), 1, 2 + Math.floor(rand() * 6));
  }
  g.globalAlpha = 1;
  speckle(g, rand, "#000", 50, 0.14);
  return c;
}

// Iron-bound treasure chest. Low enough to be looked down upon, so besides
// the front and side faces it also has a lid texture (u runs along the wall).
function chestFrontTexture() {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(13579);
  px(g, 0, 0, 16, 16, "#4a3018"); // oak body
  for (const y of [5.6, 10.8]) px(g, 0, y, 16, 0.35, "#2a1c0c"); // plank seams
  px(g, 0, 0, 16, 0.8, "#6a4426"); // lit lid edge
  px(g, 0, 4.2, 16, 0.7, "#241708"); // lid seam
  px(g, 0, 4.9, 16, 0.3, "#7c5228");
  px(g, 0, 15, 16, 1, "#241708"); // base shadow
  // iron straps
  for (const x of [2.2, 12.6]) {
    px(g, x, 0, 1.2, 16, "#2f3338");
    px(g, x, 0, 0.4, 16, "#565c66");
  }
  // lock plate with a keyhole
  px(g, 6.6, 3.2, 2.8, 3.8, "#2f3338");
  px(g, 6.6, 3.2, 2.8, 0.4, "#565c66");
  px(g, 7.6, 4.4, 0.8, 0.9, "#0f0903");
  px(g, 7.8, 5.2, 0.4, 1.1, "#0f0903");
  speckle(g, rand, "#000", 55, 0.12);
  speckle(g, rand, "#c98d4c", 14, 0.05);
  return c;
}

function chestSideTexture() {
  const c = canvas();
  const g = c.getContext("2d");
  const rand = rng(86420);
  px(g, 0, 0, 16, 16, "#422b15");
  for (const y of [5.6, 10.8]) px(g, 0, y, 16, 0.35, "#2a1c0c"); // planks
  px(g, 0, 0, 16, 0.8, "#5a3a20");
  px(g, 0, 4.2, 16, 0.7, "#241708"); // lid seam carries around
  px(g, 0, 15, 16, 1, "#241708");
  // single strap wrapping the flank
  px(g, 7.2, 0, 1.4, 16, "#2f3338");
  px(g, 7.2, 0, 0.5, 16, "#565c66");
  g.globalAlpha = 0.18;
  for (let i = 0; i < 30; i++) {
    g.fillStyle = shadeColor("#6a4426", 0.6 + rand() * 0.7);
    g.fillRect(Math.floor(rand() * TEX), Math.floor(rand() * TEX), 2 + Math.floor(rand() * 5), 1);
  }
  g.globalAlpha = 1;
  speckle(g, rand, "#000", 45, 0.14);
  return c;
}

// Trophies for the walls — a long sword, a heater shield — and a suit of
// armour standing against the masonry. Drawn by the renderer as single flat
// quads with sprite-style distance shading, so they may keep transparency.
function swordSprite() {
  // one upright blade, composed twice as a crossed pair of trophies
  const blade = canvas();
  const g0 = blade.getContext("2d");
  px(g0, 7, 0.6, 2, 1.2, "#c9a24a"); // pommel
  px(g0, 7.4, 1.8, 1.2, 2.6, "#4a3018"); // wrapped grip
  for (const y of [2.3, 3.3]) px(g0, 7.4, y, 1.2, 0.4, "#2a1c0c");
  px(g0, 5, 4.4, 6, 1.1, "#3a3f45"); // crossguard
  px(g0, 5, 4.4, 6, 0.4, "#565c66");
  px(g0, 7.2, 5.5, 1.6, 8.8, "#8d949c"); // blade, point down
  px(g0, 7.9, 5.5, 0.4, 8.8, "#c9ced4"); // fuller catching light
  px(g0, 7.5, 14.3, 1, 0.8, "#8d949c");
  px(g0, 7.7, 15.1, 0.6, 0.5, "#c9ced4"); // tip
  const c = canvas();
  const g = c.getContext("2d");
  g.imageSmoothingEnabled = false;
  for (const ang of [-0.55, 0.55]) {
    g.save();
    g.translate(32, 32);
    g.rotate(ang);
    g.drawImage(blade, -32, -32);
    g.restore();
  }
  return c;
}

function shieldSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  // steel-rimmed heater shield, tapering to a point
  px(g, 3, 1.5, 10, 8, "#565c66");
  px(g, 4, 9.5, 8, 1.5, "#565c66");
  px(g, 5, 11, 6, 1.5, "#565c66");
  px(g, 6, 12.5, 4, 1, "#565c66");
  px(g, 7, 13.5, 2, 1, "#565c66");
  px(g, 3.8, 2.3, 8.4, 6.6, "#7a1f1f"); // red field
  px(g, 4.8, 8.9, 6.4, 1.6, "#7a1f1f");
  px(g, 5.8, 10.5, 4.4, 1.4, "#7a1f1f");
  px(g, 6.8, 11.9, 2.4, 1, "#7a1f1f");
  px(g, 7.2, 3, 1.6, 7, "#e9c93c"); // gold cross
  px(g, 5, 4.6, 6, 1.6, "#e9c93c");
  px(g, 3.8, 2.3, 1.6, 0.8, "#a84040"); // polished glint
  return c;
}

function armorSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  px(g, 5.4, 14.9, 5.2, 1.1, "#241708"); // stand base
  px(g, 6.6, 0.9, 2.8, 2.4, "#565c66"); // helm
  px(g, 6.6, 0.9, 2.8, 0.5, "#8d949c");
  px(g, 6.9, 1.9, 2.2, 0.5, "#0d0b08"); // visor slit
  px(g, 4.9, 3.3, 6.2, 1.2, "#454b52"); // pauldrons
  px(g, 4.9, 3.3, 6.2, 0.4, "#6d747e");
  px(g, 5.8, 4.5, 4.4, 4.1, "#565c66"); // cuirass
  px(g, 5.8, 4.5, 1, 4.1, "#8d949c");
  px(g, 4.7, 4.5, 1, 3.8, "#454b52"); // arms hanging
  px(g, 10.3, 4.5, 1, 3.8, "#454b52");
  px(g, 4.6, 8.3, 1.2, 1.2, "#3a3f45"); // gauntlets
  px(g, 10.2, 8.3, 1.2, 1.2, "#3a3f45");
  px(g, 5.6, 8.6, 4.8, 1.6, "#3a3f45"); // faulds
  px(g, 5.6, 8.6, 4.8, 0.4, "#565c66");
  px(g, 6.1, 10.2, 1.3, 4.7, "#454b52"); // legs
  px(g, 8.6, 10.2, 1.3, 4.7, "#454b52");
  px(g, 6.1, 10.2, 0.4, 4.7, "#6d747e");
  px(g, 5.8, 14, 1.9, 0.9, "#3a3f45"); // sabatons
  px(g, 8.4, 14, 1.9, 0.9, "#3a3f45");
  return c;
}

// Display pieces for the wall niches: a weathered stone figure and a clay
// amphora, each on its own plinth. Billboards like the armour — the recess
// opening clips them per column through the z-buffer.
function statueSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  const stone = "#8a857c";
  const lit = "#a8a39a";
  const dark = "#5f5b53";
  // plinth
  px(g, 4.6, 12.2, 6.8, 0.9, lit);
  px(g, 5, 13.1, 6, 2.3, stone);
  px(g, 5, 13.1, 0.8, 2.3, lit);
  px(g, 4.6, 15.1, 6.8, 0.9, dark);
  // robe falling to the plinth, flaring at the hem
  px(g, 6.2, 5.4, 3.6, 6.8, stone);
  px(g, 5.8, 10.6, 4.4, 1.6, stone);
  px(g, 6.2, 5.4, 0.9, 6.8, lit); // fold catching the torchlight
  px(g, 9, 5.8, 0.8, 6.4, dark); // fold in shadow
  px(g, 7.6, 6.6, 0.4, 5.6, dark); // crease down the middle
  // arms folded across the chest
  px(g, 6, 6.8, 4, 1.1, stone);
  px(g, 6, 6.8, 4, 0.4, lit);
  // head with a hood's shadow across the face
  px(g, 7, 3.2, 2.2, 2.2, stone);
  px(g, 6.7, 3, 2.8, 0.7, lit);
  px(g, 7.2, 4.1, 1.8, 0.7, dark);
  // chips and stains of age
  px(g, 6.6, 9.2, 0.5, 0.5, dark);
  px(g, 8.4, 12.6, 0.6, 0.4, dark);
  return c;
}

function urnSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  const clay = "#9c5a30";
  const lit = "#c07a44";
  const dark = "#6e3d1e";
  // low plinth
  px(g, 4.8, 13.6, 6.4, 0.8, "#8a857c");
  px(g, 5.2, 14.4, 5.6, 1.2, "#5f5b53");
  // foot and belly of the amphora
  px(g, 7, 12.4, 2, 1.2, dark);
  px(g, 6, 11.6, 4, 0.9, clay);
  px(g, 5.2, 8, 5.6, 3.7, clay);
  px(g, 5.6, 7.2, 4.8, 0.9, clay);
  px(g, 5.2, 8, 1, 3.7, lit); // glaze catching the light
  px(g, 9.6, 8.2, 1, 3.4, dark);
  // painted band around the shoulder
  px(g, 5.6, 8.6, 4.8, 0.7, "#3a2a16");
  // neck and flared rim
  px(g, 7, 6, 2, 1.3, clay);
  px(g, 6.4, 5.2, 3.2, 0.9, lit);
  // handles arching from rim to shoulder
  px(g, 4.6, 6.4, 0.8, 2.2, dark);
  px(g, 5.2, 6, 1.2, 0.7, dark);
  px(g, 10.6, 6.4, 0.8, 2.2, dark);
  px(g, 9.6, 6, 1.2, 0.7, dark);
  return c;
}

// Iron chains for the gloomier corners: a pair hung on the masonry ending
// in a shackle and a ring, and a chain bolted to the ceiling with a heavy
// hook. Links alternate face-on and edge-on; the face-on holes are cleared
// so the wall (or the dark) shows through.
function chainStrand(g, x, y0, y1, sway = 0) {
  let i = 0;
  for (let y = y0; y < y1; y += 1.2, i++) {
    const cx = x + (sway ? Math.sin(i * 1.9) * sway : 0);
    if (i & 1) {
      px(g, cx - 0.2, y, 0.4, 1.5, "#31363c"); // edge-on link
      px(g, cx - 0.2, y, 0.4, 0.5, "#6d747e");
    } else {
      px(g, cx - 0.55, y, 1.1, 1.6, "#3a3f45"); // face-on link
      px(g, cx - 0.55, y, 0.35, 1.6, "#565c66");
      g.clearRect((cx - 0.2) * 4, (y + 0.45) * 4, 0.4 * 4, 0.75 * 4);
    }
  }
}

function chainWallSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  // ring plates bolted to the stone
  for (const mx of [4.5, 11]) {
    px(g, mx - 1, 0.6, 2, 1, "#2c3136");
    px(g, mx - 1, 0.6, 2, 0.35, "#565c66");
    px(g, mx - 0.35, 1.6, 0.7, 0.8, "#3a3f45");
  }
  chainStrand(g, 4.5, 2.4, 10.6, 0.35);
  chainStrand(g, 11, 2.4, 7.2, 0.3);
  // the long chain ends in an open shackle
  px(g, 3.3, 10.8, 2.4, 0.6, "#454b52");
  px(g, 3.3, 10.8, 2.4, 0.25, "#6d747e");
  px(g, 3.3, 11.4, 0.6, 1.6, "#454b52");
  px(g, 5.1, 11.4, 0.6, 1.6, "#454b52");
  px(g, 3.3, 13, 0.75, 0.5, "#31363c"); // open jaws
  px(g, 4.95, 13, 0.75, 0.5, "#31363c");
  // the short one in a dangling ring
  px(g, 10.1, 7.3, 1.8, 1.8, "#3a3f45");
  px(g, 10.1, 7.3, 1.8, 0.4, "#565c66");
  g.clearRect(10.55 * 4, 7.75 * 4, 0.9 * 4, 0.9 * 4);
  // a touch of rust where rain never reaches
  const rand = rng(913);
  g.globalAlpha = 0.3;
  for (let i = 0; i < 12; i++) {
    g.fillStyle = "#6a4426";
    g.fillRect(Math.floor(12 + rand() * 14), Math.floor(10 + rand() * 40), 2, 2);
  }
  g.globalAlpha = 1;
  return c;
}

function chainCeilingSprite() {
  const c = canvas();
  const g = c.getContext("2d");
  // mounting plate flush with the ceiling
  px(g, 6.4, 0, 3.2, 0.7, "#2c3136");
  px(g, 6.4, 0, 3.2, 0.25, "#565c66");
  chainStrand(g, 8, 0.7, 11.4, 0.25);
  // heavy meat hook at the end
  px(g, 7.65, 11.3, 0.7, 1.7, "#454b52"); // shank
  px(g, 7.65, 11.3, 0.25, 1.7, "#6d747e");
  px(g, 6.5, 12.5, 1.6, 0.7, "#454b52"); // curl of the hook
  px(g, 6.5, 11.6, 0.65, 1.2, "#454b52"); // tip rising back up
  px(g, 6.5, 11.6, 0.65, 0.4, "#8d949c"); // worn point catches light
  // a shorter strand swings beside it, ending in a bare ring
  px(g, 10.9, 0, 1.7, 0.55, "#2c3136");
  chainStrand(g, 11.7, 0.55, 5.6, 0.2);
  px(g, 10.9, 5.5, 1.6, 1.6, "#3a3f45");
  px(g, 10.9, 5.5, 1.6, 0.35, "#565c66");
  g.clearRect(11.3 * 4, 5.95 * 4, 0.8 * 4, 0.8 * 4);
  return c;
}

function chestLidTexture() {
  // plain flat colour: the lid's screen mapping is only approximate, and a
  // single tone can never show the distortion
  const c = canvas();
  const g = c.getContext("2d");
  px(g, 0, 0, 16, 16, "#553a1e");
  return c;
}

// ------------------------------------------------------------- HUD crest

// Heraldic shield that takes damage as the player does.
function crestSprite(state) {
  const c = canvas(48, 48);
  const g = c.getContext("2d");
  const P = (x, y, w, h, col) => {
    g.fillStyle = col;
    g.fillRect(x * 3, y * 3, w * 3, h * 3);
  };
  const field = state === "dead" ? "#5a5148" : "#8c1a1a";
  const edge = state === "dead" ? "#7a7268" : "#c9a24a";
  const emblem = state === "dead" ? "#8d8478" : "#e9c93c";
  // shield: border then field, tapering to a point
  P(2, 1, 12, 9, edge);
  P(3, 10, 10, 1, edge);
  P(4, 11, 8, 1, edge);
  P(5, 12, 6, 1, edge);
  P(6, 13, 4, 1, edge);
  P(7, 14, 2, 1, edge);
  P(3, 2, 10, 8, field);
  P(4, 10, 8, 1, field);
  P(5, 11, 6, 1, field);
  P(6, 12, 4, 1, field);
  P(7, 13, 2, 1, field);
  // gold cross
  P(7, 3, 2, 8, emblem);
  P(5, 5, 6, 2, emblem);

  const crack = (cells) => {
    g.fillStyle = "#241209";
    for (const [x, y, w, h] of cells) g.fillRect(x * 3, y * 3, w * 3, h * 3);
  };
  if (state === "healthy") {
    P(3, 2, 2, 1, "#ffe08a"); // polished glint
  } else if (state === "ok") {
    crack([[10, 3, 1, 1], [11, 4, 1, 1], [10, 5, 1, 1]]);
  } else if (state === "hurt") {
    crack([[10, 3, 1, 1], [11, 4, 1, 1], [10, 5, 1, 1]]);
    crack([[5, 1, 1, 2], [6, 3, 1, 2], [5, 5, 1, 2], [6, 7, 1, 2]]);
    g.clearRect(2 * 3, 1 * 3, 2 * 3, 2 * 3); // chipped corner
  } else if (state === "bad") {
    crack([[10, 2, 1, 2], [11, 4, 1, 1], [10, 5, 1, 2], [9, 7, 1, 2]]);
    crack([[5, 1, 1, 2], [6, 3, 1, 2], [5, 5, 1, 2], [6, 7, 1, 2], [5, 9, 1, 2]]);
    crack([[7, 11, 1, 2], [8, 13, 1, 1]]);
    g.clearRect(2 * 3, 1 * 3, 2 * 3, 3 * 3);
    g.clearRect(12 * 3, 1 * 3, 2 * 3, 2 * 3);
    g.clearRect(7 * 3, 14 * 3, 2 * 3, 1 * 3);
  } else if (state === "dead") {
    // split in two
    crack([[7, 1, 1, 3], [8, 4, 1, 3], [7, 7, 1, 3], [8, 10, 1, 3], [7, 13, 1, 2]]);
    g.clearRect(2 * 3, 1 * 3, 3 * 3, 2 * 3);
    g.clearRect(11 * 3, 1 * 3, 3 * 3, 2 * 3);
  }
  return c;
}

// ---------------------------------------------------------------- assemble

export function buildAssets() {
  const walls = {
    1: stoneTexture(11),
    2: stoneTexture(22, { moss: true }),
    3: timberTexture(33),
    4: dungeonTexture(44),
    5: brickTexture("#8f4f2c", "#3a2c20", 55),
    6: bannerTexture(66),
    7: arrowSlitTexture(77),
    B: barsTexture(),
    D: doorTexture(),
    R: barsTexture({ door: true }),
    X: gateTexture(),
    L: leverTexture(false),
    l: leverTexture(true),
    N: nicheTexture(121),
  };
  // the mirror's vortex churns over four frames, "~0".."~3"
  for (let f = 0; f < 4; f++) walls[`~${f}`] = portalTexture(f);
  const wallsDark = {};
  for (const k of Object.keys(walls)) wallsDark[k] = darken(walls[k], 0.65);

  const poses = ["stand", "walk1", "walk2", "aim", "fire", "pain", "die1", "die2", "dead"];
  const enemySprites = {};
  for (const [type, pal] of [["guard", PAL_GUARD], ["knight", PAL_KNIGHT], ["captain", PAL_CAPTAIN]]) {
    enemySprites[type] = {};
    for (const p of poses) enemySprites[type][p] = soldierSprite(pal, p);
  }
  enemySprites.bat = {};
  for (const p of poses) enemySprites.bat[p] = batSwarmSprite(p);
  enemySprites.dog = {};
  for (const p of poses) enemySprites.dog[p] = dogSprite(p);
  for (const p of ["stand", "walk1", "walk2"]) {
    enemySprites.dog[`${p}_right`] = enemySprites.dog[p];
    enemySprites.dog[`${p}_left`] = mirrorSprite(enemySprites.dog[p]);
    enemySprites.dog[`${p}_front`] = dogFrontSprite(p);
    enemySprites.dog[`${p}_back`] = dogBackSprite(p);
  }

  const items = {};
  for (const k of ["key", "potion", "bread", "bolts", "treasure", "arbalest"]) {
    items[k] = itemSprite(k);
  }
  items.boltsDrop = items.bolts;

  const trapSprites = {
    plate: trapSprite("plate"),
    pressed: trapSprite("pressed"),
    arrow: trapSprite("arrow"),
    arrowLeft: trapSprite("arrowLeft"),
    arrowToward: trapSprite("arrowToward"),
    arrowAway: trapSprite("arrowAway"),
  };

  const weapons = {
    dagger: [weaponView("dagger", 0), weaponView("dagger", 1), weaponView("dagger", 2)],
    crossbow: [weaponView("crossbow", 0), weaponView("crossbow", 1), weaponView("crossbow", 2)],
    arbalest: [weaponView("arbalest", 0), weaponView("arbalest", 1), weaponView("arbalest", 2)],
  };

  const weaponIcons = {
    dagger: weaponIcon("dagger"),
    crossbow: weaponIcon("crossbow"),
    arbalest: weaponIcon("arbalest"),
  };

  const crests = {};
  for (const s of ["healthy", "ok", "hurt", "bad", "dead"]) crests[s] = crestSprite(s);

  const torches = [0, 1, 2, 3].map(torchSprite);
  const fountains = [0, 1, 2, 3].map(fountainSprite);

  // armour is a free-standing billboard like the furniture; ceiling chains
  // are billboards too, just anchored to the ceiling by the renderer
  const decor = {
    table: tableSprite(),
    chair: chairSprite(),
    armor: armorSprite(),
    chains: chainCeilingSprite(),
    statue: statueSprite(),
    urn: urnSprite(),
  };

  // wall-box furniture; faces get the same orientation shading as walls
  // (darker along y), lids are horizontal and need no dark variant
  const boxes = {
    wardrobe: { front: wardrobeFrontTexture(), side: wardrobeSideTexture() },
    chest: { front: chestFrontTexture(), side: chestSideTexture(), lid: chestLidTexture() },
  };
  for (const b of Object.values(boxes)) {
    b.frontDark = darken(b.front, 0.65);
    b.sideDark = darken(b.side, 0.65);
  }

  const hangings = { sword: swordSprite(), shield: shieldSprite(), chain: chainWallSprite() };

  return { walls, wallsDark, enemySprites, items, trapSprites, weapons, weaponIcons, crests, torches, fountains, decor, boxes, hangings, TEX };
}
