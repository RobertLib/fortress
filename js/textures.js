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

// -------------------------------------------------------- weapon view (POV)

function weaponView(kind, frame) {
  const c = canvas(128, 128);
  const g = c.getContext("2d");
  const P = (x, y, w, h, col) => {
    g.fillStyle = col;
    g.fillRect(x * 4, y * 4, w * 4, h * 4);
  };
  const skin = "#dba377";
  const skinD = "#b98457";
  if (kind === "dagger") {
    const up = frame === 1 ? -6 : frame === 2 ? -3 : 0;
    P(13, 26 + up, 7, 6, skin); // fist
    P(13, 26 + up, 7, 1.5, skinD);
    P(14, 30 + up, 5, 2, skinD); // wrist shade
    P(14.5, 21 + up, 4, 5, "#3a2a16"); // wrapped grip
    P(14.5, 22.5 + up, 4, 0.8, "#5a4326");
    P(14.5, 24.5 + up, 4, 0.8, "#5a4326");
    P(13, 19.5 + up, 7, 1.5, "#c9a24a"); // crossguard
    P(15.25, 9 + up, 2.5, 10.5, "#c9d4d8"); // blade
    P(15.25, 9 + up, 1, 10.5, "#eef4f6");
    P(15.25, 8 + up, 2.5, 1.5, "#eef4f6"); // tip
  } else if (kind === "crossbow") {
    const up = frame >= 1 ? 2 : 0;
    // forearm + hand on the tiller
    P(12, 28 + up, 8, 4, skin);
    P(11.5, 25 + up, 9, 3.5, skin);
    P(11.5, 25 + up, 9, 1, skinD);
    P(10.5, 26.5 + up, 1.5, 2, skinD); // thumb
    // tiller (stock): wide near the eye, tapering away
    P(14, 18 + up, 4, 7.5, "#4a3018");
    P(14, 18 + up, 1.2, 7.5, "#6a4a28");
    P(14.6, 13 + up, 2.8, 5, "#553a20");
    // thick bow limbs with upswept tips and a steel lath
    P(7, 12.5 + up, 18, 2.4, "#3a2a16");
    P(5.5, 11.5 + up, 2, 2.6, "#33220f");
    P(24.5, 11.5 + up, 2, 2.6, "#33220f");
    P(7, 12.9 + up, 18, 0.8, "#5d4326");
    P(7, 12.1 + up, 18, 0.5, "#9aa2ac");
    if (frame === 0) {
      // string drawn back to the nut, bolt loaded
      const str = "#9a927e";
      P(7, 14.9, 3.2, 0.6, str);
      P(10.2, 16.1, 3, 0.6, str);
      P(13.2, 17.2, 2.2, 0.6, str);
      P(21.8, 14.9, 3.2, 0.6, str);
      P(18.8, 16.1, 3, 0.6, str);
      P(16.6, 17.2, 2.2, 0.6, str);
      P(15.55, 10, 0.9, 7.5, "#7a5a30"); // bolt shaft
      P(15.3, 8.2, 1.4, 1.9, "#8d949c"); // steel head
      P(15.1, 15.6, 1.8, 1.8, "#a03030"); // fletching
    } else {
      // loosed: string snapped forward against the lath
      P(7, 13 + up, 18, 0.6, "#b8b09a");
      if (frame === 1) {
        P(15.7, 3.5, 0.6, 4, "#e8e0c8"); // bolt streak
        P(15.7, 2, 0.6, 1, "#ffffff");
      }
    }
  } else if (kind === "arbalest") {
    const up = frame >= 1 ? 1.5 : 0;
    // two hands
    P(9.5, 27 + up, 6, 5, skin);
    P(9.5, 27 + up, 6, 1.2, skinD);
    P(16.5, 27 + up, 6, 5, skin);
    P(16.5, 27 + up, 6, 1.2, skinD);
    // stock
    P(13.5, 17 + up, 5, 10, "#4a3018");
    P(13.5, 17 + up, 1.2, 10, "#6a4a28");
    // magazine box on top (repeating mechanism)
    P(12.5, 9.5 + up, 7, 7, "#5d4326");
    P(12.5, 9.5 + up, 7, 1, "#7a5a34");
    P(13.3, 11.5 + up, 5.4, 0.8, "#3c2a14");
    P(13.3, 13.5 + up, 5.4, 0.8, "#3c2a14");
    P(12.5, 9.5 + up, 1, 7, "#7a5a34");
    // thick wide limbs with steel lath
    P(6, 16 + up, 20, 2.6, "#3a2a16");
    P(4.5, 15 + up, 2, 3, "#33220f");
    P(25.5, 15 + up, 2, 3, "#33220f");
    P(6, 16.5 + up, 20, 0.8, "#5d4326");
    P(6, 15.6 + up, 20, 0.5, "#9aa2ac");
    P(6.5, 18.8 + up, 19, 0.6, "#b8b09a"); // string
    if (frame === 1) {
      P(15.4, 3.5, 1, 4.5, "#e8e0c8"); // bolt streak
      P(15.4, 2, 1, 1, "#ffffff");
    }
  }
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
    D: doorTexture(),
    X: gateTexture(),
  };
  const wallsDark = {};
  for (const k of Object.keys(walls)) wallsDark[k] = darken(walls[k], 0.65);

  const poses = ["stand", "walk1", "walk2", "aim", "fire", "pain", "die1", "die2", "dead"];
  const enemySprites = {};
  for (const [type, pal] of [["guard", PAL_GUARD], ["knight", PAL_KNIGHT], ["captain", PAL_CAPTAIN]]) {
    enemySprites[type] = {};
    for (const p of poses) enemySprites[type][p] = soldierSprite(pal, p);
  }

  const items = {};
  for (const k of ["key", "potion", "bread", "bolts", "treasure", "arbalest"]) {
    items[k] = itemSprite(k);
  }
  items.boltsDrop = items.bolts;

  const weapons = {
    dagger: [weaponView("dagger", 0), weaponView("dagger", 1), weaponView("dagger", 2)],
    crossbow: [weaponView("crossbow", 0), weaponView("crossbow", 1), weaponView("crossbow", 2)],
    arbalest: [weaponView("arbalest", 0), weaponView("arbalest", 1), weaponView("arbalest", 2)],
  };

  const crests = {};
  for (const s of ["healthy", "ok", "hurt", "bad", "dead"]) crests[s] = crestSprite(s);

  return { walls, wallsDark, enemySprites, items, weapons, crests, TEX };
}
