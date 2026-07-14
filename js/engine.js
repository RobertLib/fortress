// Raycasting renderer: walls via DDA (with sliding doors), billboard
// sprites clipped against a z-buffer, POV weapon, HUD and automap.
// Torchlit atmosphere: everything fades into darkness with distance.

import { darken } from "./textures.js";

export const W = 640;
export const H = 400;
export const VIEW_H = 320; // 3D viewport; the rest is the HUD bar
const MAX_STEPS = 96;

const FOG = "10,8,6"; // rgb of the darkness color
const fogAmount = (dist) => Math.min(0.88, Math.max(0, (dist - 1.4) / 13));

// wall-box furniture: shallow boxes standing flush against walls, in
// fractions of a wall cell. Boxes lower than the eye (0.5) also get a lid.
const BOX_SPECS = {
  wardrobe: { halfW: 0.46, depth: 0.22, height: 0.8 },
  chest: { halfW: 0.31, depth: 0.24, height: 0.42 },
};

// wall hangings: flat trophies just off the wall face, one oriented quad
// each, spanning zBot..zTop of the wall height
const HANGING_SPECS = {
  sword: { zBot: 0.3, zTop: 0.9, offset: 0.03 },
  shield: { zBot: 0.35, zTop: 0.85, offset: 0.03 },
  chain: { zBot: 0.22, zTop: 0.92, offset: 0.03 },
};

// distance buckets for pre-darkened sprite variants
const SPRITE_SHADES = [1, 0.78, 0.58, 0.42];
const shadeBucket = (dist) => (dist < 3.5 ? 0 : dist < 6 ? 1 : dist < 9 ? 2 : 3);

const torchFlicker = (time, phase) =>
  0.84 + Math.sin(time * 17 + phase) * 0.1 + Math.sin(time * 29 + phase * 1.7) * 0.06;

function torchLight(level, x, y, time) {
  let light = 0;
  for (const t of level.torches) {
    const dist = Math.hypot(t.x - x, t.y - y);
    if (dist >= 3.4) continue;
    light = Math.max(light, (1 - dist / 3.4) * torchFlicker(time, t.phase));
  }
  return light;
}

export class Renderer {
  constructor(canvas, assets) {
    this.ctx = canvas.getContext("2d");
    this.ctx.imageSmoothingEnabled = false;
    this.assets = assets;
    this.zbuffer = new Float32Array(W);
    this.transparentWalls = new Array(W);
    this.spriteShades = new Map(); // img -> [img, darker, ...]
    this.gradCache = null;

    // static vignette overlay for the 3D view
    this.vignette = document.createElement("canvas");
    this.vignette.width = W;
    this.vignette.height = VIEW_H;
    const vg = this.vignette.getContext("2d");
    const grad = vg.createRadialGradient(W / 2, VIEW_H / 2, VIEW_H * 0.45, W / 2, VIEW_H / 2, W * 0.62);
    grad.addColorStop(0, "rgba(0,0,0,0)");
    grad.addColorStop(1, "rgba(6,4,2,0.5)");
    vg.fillStyle = grad;
    vg.fillRect(0, 0, W, VIEW_H);
  }

  shadedSprite(img, bucket) {
    if (bucket === 0) return img;
    let arr = this.spriteShades.get(img);
    if (!arr) {
      arr = [img];
      this.spriteShades.set(img, arr);
    }
    if (!arr[bucket]) arr[bucket] = darken(img, SPRITE_SHADES[bucket]);
    return arr[bucket];
  }

  // ------------------------------------------------------------- 3D view

  renderView(game) {
    const g = this.ctx;
    const level = game.level;
    const p = game.player;

    // ceiling and floor sink into darkness at the horizon
    if (!this.gradCache || this.gradCache.level !== level) {
      const ceil = g.createLinearGradient(0, 0, 0, VIEW_H / 2);
      ceil.addColorStop(0, level.ceilColor);
      ceil.addColorStop(1, `rgb(${FOG})`);
      const floor = g.createLinearGradient(0, VIEW_H / 2, 0, VIEW_H);
      floor.addColorStop(0, `rgb(${FOG})`);
      floor.addColorStop(0.25, level.floorColor);
      floor.addColorStop(1, level.floorColor);
      this.gradCache = { level, ceil, floor };
    }
    g.fillStyle = this.gradCache.ceil;
    g.fillRect(0, 0, W, VIEW_H / 2);
    g.fillStyle = this.gradCache.floor;
    g.fillRect(0, VIEW_H / 2, W, VIEW_H / 2);

    for (let x = 0; x < W; x++) {
      const cameraX = (2 * x) / W - 1;
      const rayDirX = p.dirX + p.planeX * cameraX;
      const rayDirY = p.dirY + p.planeY * cameraX;
      const hit = this.castRay(game, p.x, p.y, rayDirX, rayDirY);
      this.zbuffer[x] = hit.dist;
      this.transparentWalls[x] = hit.overlays;
      if (hit.dist <= 0.0001) continue;

      const lineHeight = VIEW_H / hit.dist;
      const drawStart = VIEW_H / 2 - lineHeight / 2;
      const texSet = hit.side === 1 ? this.assets.wallsDark : this.assets.walls;
      const tex = texSet[hit.tex] ?? texSet["1"];
      let texX = Math.floor(hit.wallX * 64);
      if (texX < 0) texX = 0;
      if (texX > 63) texX = 63;
      g.drawImage(tex, texX, 0, 1, 64, x, drawStart, 1, lineHeight);

      const hitX = p.x + rayDirX * hit.dist;
      const hitY = p.y + rayDirY * hit.dist;
      const light = torchLight(level, hitX, hitY, game.time);
      if (light > 0.015) {
        g.fillStyle = `rgba(255,132,32,${Math.min(0.16, light * 0.14)})`;
        g.fillRect(x, drawStart, 1, lineHeight);
      }
      const fog = Math.max(0, fogAmount(hit.dist) - light * 0.42);
      if (fog > 0.02) {
        g.fillStyle = `rgba(${FOG},${fog})`;
        g.fillRect(x, drawStart, 1, lineHeight);
      }
    }

    // Wall boxes (wardrobes, chests) are opaque geometry: drawn right after
    // the walls, z-tested and written per column, so bars and sprites clip
    // against them too.
    this.renderWallBoxes(game);
    this.renderWallHangings(game);

    // Transparent walls are initially drawn over the opaque scene. Sprites are
    // composited afterward and redraw only the bars that are closer than them.
    // This keeps actors correctly in front of or behind a barred surface.
    this.renderTransparentWalls();
    this.renderTorchGlows(game);
    this.renderSprites(game);
    g.drawImage(this.vignette, 0, 0);
    this.renderWeapon(game);
    this.renderFlashes(game);
  }

  castRay(game, posX, posY, rayDirX, rayDirY) {
    const { grid, w, h } = game.level;
    let mapX = Math.floor(posX);
    let mapY = Math.floor(posY);
    const deltaX = Math.abs(1 / rayDirX);
    const deltaY = Math.abs(1 / rayDirY);
    let stepX, stepY, sideDistX, sideDistY;
    if (rayDirX < 0) {
      stepX = -1;
      sideDistX = (posX - mapX) * deltaX;
    } else {
      stepX = 1;
      sideDistX = (mapX + 1 - posX) * deltaX;
    }
    if (rayDirY < 0) {
      stepY = -1;
      sideDistY = (posY - mapY) * deltaY;
    } else {
      stepY = 1;
      sideDistY = (mapY + 1 - posY) * deltaY;
    }

    let side = 0;
    const overlays = [];
    for (let i = 0; i < MAX_STEPS; i++) {
      if (sideDistX < sideDistY) {
        sideDistX += deltaX;
        mapX += stepX;
        side = 0;
      } else {
        sideDistY += deltaY;
        mapY += stepY;
        side = 1;
      }
      if (mapX < 0 || mapY < 0 || mapX >= w || mapY >= h) break;
      const cell = grid[mapY * w + mapX];
      if (cell === ".") continue;

      if (cell === "D" || cell === "R") {
        // sliding door: test the plane in the middle of the cell
        const open = game.doorOpenAmount(mapX, mapY);
        let distMid, coord;
        if (side === 0) {
          distMid = (mapX - posX + (1 - stepX) / 2 + stepX * 0.5) / rayDirX;
          coord = posY + distMid * rayDirY - mapY;
        } else {
          distMid = (mapY - posY + (1 - stepY) / 2 + stepY * 0.5) / rayDirY;
          coord = posX + distMid * rayDirX - mapX;
        }
        if (coord >= 0 && coord < 1 && coord >= open) {
          if (cell === "R") {
            overlays.push({ dist: distMid, side, tex: "R", wallX: coord - open });
            continue;
          }
          return { dist: distMid, side, tex: "D", wallX: coord - open, overlays };
        }
        continue; // door open at this spot — ray passes through
      }

      if (cell === "N") {
        // niche: the face is carved back to the middle of the cell, tested
        // door-style on the mid plane; rays that clip past the opening run
        // on into the neighbours, whose sides become the jambs
        let distMid, coord;
        if (side === 0) {
          distMid = (mapX - posX + (1 - stepX) / 2 + stepX * 0.5) / rayDirX;
          coord = posY + distMid * rayDirY - mapY;
        } else {
          distMid = (mapY - posY + (1 - stepY) / 2 + stepY * 0.5) / rayDirY;
          coord = posX + distMid * rayDirX - mapX;
        }
        if (coord >= 0 && coord < 1) {
          return { dist: distMid, side, tex: "N", wallX: coord, overlays };
        }
        continue;
      }

      // solid wall (or exit door "X")
      let dist, wallX;
      if (side === 0) {
        dist = (mapX - posX + (1 - stepX) / 2) / rayDirX;
        wallX = posY + dist * rayDirY;
      } else {
        dist = (mapY - posY + (1 - stepY) / 2) / rayDirY;
        wallX = posX + dist * rayDirX;
      }
      wallX -= Math.floor(wallX);
      if ((side === 0 && rayDirX > 0) || (side === 1 && rayDirY < 0)) {
        wallX = 1 - wallX;
      }
      if (cell === "B") {
        overlays.push({ dist, side, tex: "B", wallX });
        continue;
      }
      if (cell === "~") {
        // mirror portal: the vortex in the glass churns through four
        // texture frames keyed to the game clock
        return { dist, side, tex: `~${Math.floor(game.time * 7) & 3}`, wallX, overlays };
      }
      if (cell === "Z") {
        // secret wall: disguised in the neighbours' masonry, and unlike the
        // recessed doors it sits flush on the cell face; when its lever is
        // pulled it slides aside exactly like a door leaf
        const door = game.doorAt(mapX, mapY);
        const open = door?.open ?? 0;
        if (wallX >= open) {
          return { dist, side, tex: door?.tex ?? "1", wallX: wallX - open, overlays };
        }
        continue; // ray slips through the revealed gap
      }
      if (cell === "L") {
        // the handle hangs on a single face; the ray sees it only when it
        // struck that face — every other side is plain masonry
        const lever = game.leverAt(mapX, mapY);
        const faceX = side === 0 ? -stepX : 0;
        const faceY = side === 1 ? -stepY : 0;
        const onFace = lever && lever.dx === faceX && lever.dy === faceY;
        const tex = onFace ? (lever.pulled ? "l" : "L") : lever?.tex ?? "1";
        return { dist, side, tex, wallX, overlays };
      }
      return { dist, side, tex: cell, wallX, overlays };
    }
    return { dist: 1e9, side: 0, tex: "1", wallX: 0, overlays };
  }

  drawTransparentStripe(hit, x) {
    if (hit.dist <= 0.0001) return;
    const g = this.ctx;
    const lineHeight = VIEW_H / hit.dist;
    const drawStart = VIEW_H / 2 - lineHeight / 2;
    const texSet = hit.side === 1 ? this.assets.wallsDark : this.assets.walls;
    const tex = texSet[hit.tex];
    let texX = Math.floor(hit.wallX * 64);
    texX = Math.max(0, Math.min(63, texX));
    g.drawImage(tex, texX, 0, 1, 64, x, drawStart, 1, lineHeight);
  }

  renderTransparentWalls(startX = 0, endX = W - 1, inFrontOf = Infinity) {
    for (let x = Math.max(0, startX); x <= Math.min(W - 1, endX); x++) {
      const layers = this.transparentWalls[x];
      if (!layers) continue;
      // Rays encounter bars near-to-far; painters need the reverse order.
      for (let i = layers.length - 1; i >= 0; i--) {
        if (layers[i].dist < inFrontOf) this.drawTransparentStripe(layers[i], x);
      }
    }
  }

  // A wall box (wardrobe, chest) stands flush against a wall cell: a front
  // face and two short sides, each a vertical quad, plus a lid when the box
  // is low enough to be looked down upon. The wall behind keeps its normal
  // texture; the box simply draws over part of it. After backface culling
  // the visible faces never overlap on screen, so draw order within one box
  // does not matter — except the lid, drawn first so its own front face
  // (sharing the entry depth) cannot z-reject it.
  renderWallBoxes(game) {
    for (const wd of game.level.wallBoxes) {
      const spec = BOX_SPECS[wd.kind];
      const texs = this.assets.boxes[wd.kind];
      if (spec.height < 0.5 && texs.lid) this.drawBoxLid(game, wd, spec, texs.lid);
      const fx = wd.x + 0.5 + wd.dx * 0.5; // centre of the wall face
      const fy = wd.y + 0.5 + wd.dy * 0.5;
      const tx = -wd.dy; // tangent along the wall
      const ty = wd.dx;
      const ax = fx + tx * spec.halfW; // corners at the wall
      const ay = fy + ty * spec.halfW;
      const bx = fx - tx * spec.halfW;
      const by = fy - ty * spec.halfW;
      const nx = wd.dx * spec.depth; // out to the front corners
      const ny = wd.dy * spec.depth;
      this.drawBoxFace(game, ax + nx, ay + ny, bx + nx, by + ny, wd.dx, wd.dy, texs, "front", spec.height);
      this.drawBoxFace(game, ax + nx, ay + ny, ax, ay, tx, ty, texs, "side", spec.height);
      this.drawBoxFace(game, bx + nx, by + ny, bx, by, -tx, -ty, texs, "side", spec.height);
    }
  }

  // Perspective-correct textured vertical quad from (x0,y0) to (x1,y1) with
  // outward normal (nx,ny), lit and fogged exactly like the walls.
  drawBoxFace(game, x0, y0, x1, y1, nx, ny, texs, kind, HEIGHT) {
    const p = game.player;
    if (((x0 + x1) / 2 - p.x) * nx + ((y0 + y1) / 2 - p.y) * ny >= 0) return; // backface

    const invDet = 1 / (p.planeX * p.dirY - p.dirX * p.planeY);
    const toCam = (wx, wy, u) => {
      const sx = wx - p.x;
      const sy = wy - p.y;
      return { x: invDet * (p.dirY * sx - p.dirX * sy), y: invDet * (-p.planeY * sx + p.planeX * sy), u };
    };
    let a = toCam(x0, y0, 0);
    let b = toCam(x1, y1, 1);
    const NEAR = 0.05;
    if (a.y <= NEAR && b.y <= NEAR) return;
    const clip = (out, inn) => {
      const t = (NEAR - out.y) / (inn.y - out.y);
      return { x: out.x + (inn.x - out.x) * t, y: NEAR, u: out.u + (inn.u - out.u) * t };
    };
    if (a.y < NEAR) a = clip(a, b);
    else if (b.y < NEAR) b = clip(b, a);
    let sxa = (W / 2) * (1 + a.x / a.y);
    let sxb = (W / 2) * (1 + b.x / b.y);
    if (sxa > sxb) {
      [a, b] = [b, a];
      [sxa, sxb] = [sxb, sxa];
    }
    const xStart = Math.max(0, Math.ceil(sxa));
    const xEnd = Math.min(W - 1, Math.floor(sxb));
    if (xEnd < xStart) return;

    // faces along the y axis take the darker set, like side==1 walls
    const dark = Math.abs(ny) > Math.abs(nx);
    const tex = kind === "front" ? (dark ? texs.frontDark : texs.front) : (dark ? texs.sideDark : texs.side);

    const g = this.ctx;
    const level = game.level;
    const span = sxb - sxa;
    const invYa = 1 / a.y;
    const invYb = 1 / b.y;
    const uOverYa = a.u * invYa;
    const uOverYb = b.u * invYb;
    for (let x = xStart; x <= xEnd; x++) {
      const t = (x - sxa) / span;
      const dist = 1 / (invYa + (invYb - invYa) * t);
      if (dist >= this.zbuffer[x]) continue;
      const u = (uOverYa + (uOverYb - uOverYa) * t) * dist;
      const lineHeight = VIEW_H / dist;
      const top = VIEW_H / 2 + lineHeight * (0.5 - HEIGHT);
      const colH = lineHeight * HEIGHT;
      let texX = Math.floor(u * 64);
      if (texX < 0) texX = 0;
      if (texX > 63) texX = 63;
      g.drawImage(tex, texX, 0, 1, 64, x, top, 1, colH);
      this.zbuffer[x] = dist;

      const hx = x0 + (x1 - x0) * u;
      const hy = y0 + (y1 - y0) * u;
      const light = torchLight(level, hx, hy, game.time);
      if (light > 0.015) {
        g.fillStyle = `rgba(255,132,32,${Math.min(0.16, light * 0.14)})`;
        g.fillRect(x, top, 1, colH);
      }
      const fog = Math.max(0, fogAmount(dist) - light * 0.42);
      if (fog > 0.02) {
        g.fillStyle = `rgba(${FOG},${fog})`;
        g.fillRect(x, top, 1, colH);
      }
    }
  }

  // Horizontal lid of a low box, seen because the eye (0.5) sits above it.
  // Per column the ray is intersected with the box footprint (slab test in
  // the box frame); the [near, far] depth span projects to a vertical screen
  // strip filled from one lid texture column. With the unnormalised ray
  // direction dir + plane*cameraX, the ray parameter IS the perpendicular
  // depth, so it compares directly against the z-buffer. The lid never
  // writes depth: it always sits directly above its own faces on screen.
  drawBoxLid(game, wd, spec, texLid) {
    const g = this.ctx;
    const p = game.player;
    const level = game.level;
    const fx = wd.x + 0.5 + wd.dx * 0.5; // centre of the wall face
    const fy = wd.y + 0.5 + wd.dy * 0.5;
    const tx = -wd.dy;
    const ty = wd.dx;
    // player position in the box frame: n out from the wall, t along it
    const pn = (p.x - fx) * wd.dx + (p.y - fy) * wd.dy;
    const pt = (p.x - fx) * tx + (p.y - fy) * ty;

    // conservative column range from the four footprint corners
    const invDet = 1 / (p.planeX * p.dirY - p.dirX * p.planeY);
    let sx0 = W;
    let sx1 = -1;
    let clipped = false;
    for (const [n, t] of [[0, -spec.halfW], [0, spec.halfW], [spec.depth, -spec.halfW], [spec.depth, spec.halfW]]) {
      const wx = fx + wd.dx * n + tx * t - p.x;
      const wy = fy + wd.dy * n + ty * t - p.y;
      const camY = invDet * (-p.planeY * wx + p.planeX * wy);
      if (camY <= 0.05) {
        clipped = true;
        continue;
      }
      const sx = (W / 2) * (1 + (invDet * (p.dirY * wx - p.dirX * wy)) / camY);
      sx0 = Math.min(sx0, sx);
      sx1 = Math.max(sx1, sx);
    }
    if (clipped) {
      sx0 = 0;
      sx1 = W - 1;
    }
    const xStart = Math.max(0, Math.floor(sx0));
    const xEnd = Math.min(W - 1, Math.ceil(sx1));

    for (let x = xStart; x <= xEnd; x++) {
      const cameraX = (2 * x) / W - 1;
      const rdx = p.dirX + p.planeX * cameraX;
      const rdy = p.dirY + p.planeY * cameraX;
      const rn = rdx * wd.dx + rdy * wd.dy;
      const rt = rdx * tx + rdy * ty;
      let sNear = 0.05;
      let sFar = Infinity;
      if (Math.abs(rn) < 1e-9) {
        if (pn < 0 || pn > spec.depth) continue;
      } else {
        const a = (0 - pn) / rn;
        const b = (spec.depth - pn) / rn;
        sNear = Math.max(sNear, Math.min(a, b));
        sFar = Math.max(a, b);
      }
      if (Math.abs(rt) < 1e-9) {
        if (pt < -spec.halfW || pt > spec.halfW) continue;
      } else {
        const a = (-spec.halfW - pt) / rt;
        const b = (spec.halfW - pt) / rt;
        sNear = Math.max(sNear, Math.min(a, b));
        sFar = Math.min(sFar, Math.max(a, b));
      }
      if (sFar <= sNear || sNear >= this.zbuffer[x]) continue;

      const rise = (0.5 - spec.height) * VIEW_H; // eye sits above the lid
      const yTop = VIEW_H / 2 + rise / sFar;
      const yBot = VIEW_H / 2 + rise / sNear;
      if (yBot - yTop < 0.05) continue;
      const sMid = (sNear + sFar) / 2;
      const u = (pt + sMid * rt + spec.halfW) / (spec.halfW * 2);
      let texX = Math.floor(u * 64);
      if (texX < 0) texX = 0;
      if (texX > 63) texX = 63;
      g.drawImage(texLid, texX, 0, 1, 64, x, yTop, 1, yBot - yTop);

      const hx = p.x + rdx * sMid;
      const hy = p.y + rdy * sMid;
      const light = torchLight(level, hx, hy, game.time);
      if (light > 0.015) {
        g.fillStyle = `rgba(255,132,32,${Math.min(0.16, light * 0.14)})`;
        g.fillRect(x, yTop, 1, yBot - yTop);
      }
      const fog = Math.max(0, fogAmount(sMid) - light * 0.42);
      if (fog > 0.02) {
        g.fillStyle = `rgba(${FOG},${fog})`;
        g.fillRect(x, yTop, 1, yBot - yTop);
      }
    }
  }

  // Flat decorations against the wall: one perspective-correct quad each,
  // z-tested per column (so boxes and walls occlude them) but never written —
  // nothing can stand between a hanging and its wall. Transparent textures
  // rule out the walls' fog fills, so they shade like sprites instead:
  // pre-darkened distance buckets, lifted near torches.
  renderWallHangings(game) {
    const p = game.player;
    for (const hg of game.level.hangings) {
      const spec = HANGING_SPECS[hg.kind];
      const halfW = (spec.zTop - spec.zBot) / 2; // square quad, square texture
      const fx = hg.x + 0.5 + hg.dx * (0.5 + spec.offset);
      const fy = hg.y + 0.5 + hg.dy * (0.5 + spec.offset);
      if ((fx - p.x) * hg.dx + (fy - p.y) * hg.dy >= 0) continue; // backface
      const tx = -hg.dy;
      const ty = hg.dx;
      let bucket = shadeBucket(Math.hypot(fx - p.x, fy - p.y));
      const light = torchLight(game.level, fx, fy, game.time);
      if (light > 0.5) bucket = Math.max(0, bucket - 2);
      else if (light > 0.15) bucket = Math.max(0, bucket - 1);
      const tex = this.shadedSprite(this.assets.hangings[hg.kind], bucket);
      this.drawHangingQuad(game, fx + tx * halfW, fy + ty * halfW, fx - tx * halfW, fy - ty * halfW, tex, spec);
    }
  }

  drawHangingQuad(game, x0, y0, x1, y1, tex, spec) {
    const p = game.player;
    const invDet = 1 / (p.planeX * p.dirY - p.dirX * p.planeY);
    const toCam = (wx, wy, u) => {
      const sx = wx - p.x;
      const sy = wy - p.y;
      return { x: invDet * (p.dirY * sx - p.dirX * sy), y: invDet * (-p.planeY * sx + p.planeX * sy), u };
    };
    let a = toCam(x0, y0, 0);
    let b = toCam(x1, y1, 1);
    const NEAR = 0.05;
    if (a.y <= NEAR && b.y <= NEAR) return;
    const clip = (out, inn) => {
      const t = (NEAR - out.y) / (inn.y - out.y);
      return { x: out.x + (inn.x - out.x) * t, y: NEAR, u: out.u + (inn.u - out.u) * t };
    };
    if (a.y < NEAR) a = clip(a, b);
    else if (b.y < NEAR) b = clip(b, a);
    let sxa = (W / 2) * (1 + a.x / a.y);
    let sxb = (W / 2) * (1 + b.x / b.y);
    if (sxa > sxb) {
      [a, b] = [b, a];
      [sxa, sxb] = [sxb, sxa];
    }
    const xStart = Math.max(0, Math.ceil(sxa));
    const xEnd = Math.min(W - 1, Math.floor(sxb));
    if (xEnd < xStart) return;

    const g = this.ctx;
    const span = sxb - sxa;
    const invYa = 1 / a.y;
    const invYb = 1 / b.y;
    const uOverYa = a.u * invYa;
    const uOverYb = b.u * invYb;
    for (let x = xStart; x <= xEnd; x++) {
      const t = (x - sxa) / span;
      const dist = 1 / (invYa + (invYb - invYa) * t);
      if (dist >= this.zbuffer[x]) continue;
      const u = (uOverYa + (uOverYb - uOverYa) * t) * dist;
      const lineHeight = VIEW_H / dist;
      const top = VIEW_H / 2 + lineHeight * (0.5 - spec.zTop);
      const colH = lineHeight * (spec.zTop - spec.zBot);
      let texX = Math.floor(u * 64);
      if (texX < 0) texX = 0;
      if (texX > 63) texX = 63;
      g.drawImage(tex, texX, 0, 1, 64, x, top, 1, colH);
    }
  }

  renderSprites(game) {
    const g = this.ctx;
    const p = game.player;
    const sprites = [];
    for (const e of game.enemies) {
      // flying enemies carry a zCenter (their height); footmen stand on the floor
      sprites.push({ x: e.x, y: e.y, img: e.sprite(this.assets, p), scale: e.stats.scale ?? 0.82, zCenter: e.flyHeight });
    }
    for (const it of game.items) {
      const scale = it.kind === "key" ? 0.55 : 0.45;
      sprites.push({ x: it.x, y: it.y, img: this.assets.items[it.kind], scale });
    }
    for (const t of game.traps) {
      const img = t.state === "armed" ? this.assets.trapSprites.plate : this.assets.trapSprites.pressed;
      sprites.push({ x: t.x + 0.5, y: t.y + 0.5, img, scale: 0.6 });
    }
    for (const a of game.projectiles) {
      // pick the arrow view from its flight direction in camera space:
      // depth along the view axis vs. lateral drift across the screen
      const depth = a.dirX * p.dirX + a.dirY * p.dirY;
      const lat = (a.dirX * p.planeX + a.dirY * p.planeY) / 0.66;
      const s = this.assets.trapSprites;
      let img;
      if (depth < -Math.abs(lat)) img = s.arrowToward;
      else if (depth > Math.abs(lat)) img = s.arrowAway;
      else img = lat > 0 ? s.arrow : s.arrowLeft;
      sprites.push({ x: a.x, y: a.y, img, scale: 0.82 });
    }
    for (const d of game.level.decorations) {
      if (d.kind === "fountain") {
        // the water animates like the torch flames, just at a calmer pace
        const frame = Math.floor(game.time * 6) & 3;
        sprites.push({ x: d.x, y: d.y, img: this.assets.fountains[frame], scale: 0.85 });
        continue;
      }
      if (d.kind === "chains") {
        // hung from above: anchored so the mounting plate touches the ceiling
        sprites.push({ x: d.x, y: d.y, img: this.assets.decor.chains, scale: 0.55, zCenter: 1 - 0.55 / 2 });
        continue;
      }
      if (!this.assets.decor[d.kind]) continue; // wall boxes are drawn as quads, not billboards
      const scale =
        d.kind === "table" ? 0.7 : d.kind === "armor" ? 0.78 : d.kind === "statue" ? 0.72 : d.kind === "urn" ? 0.5 : 0.64;
      sprites.push({ x: d.x, y: d.y, img: this.assets.decor[d.kind], scale });
    }
    for (const t of game.level.torches) {
      const frame = Math.floor(game.time * 12 + t.phase * 1.7) & 3;
      sprites.push({
        x: t.x,
        y: t.y,
        img: this.assets.torches[frame],
        scale: 0.52,
        zCenter: 0.58,
        fullbright: true,
      });
    }
    for (const s of sprites) {
      s.dist = (p.x - s.x) ** 2 + (p.y - s.y) ** 2;
    }
    sprites.sort((a, b) => b.dist - a.dist);

    const invDet = 1 / (p.planeX * p.dirY - p.dirX * p.planeY);
    for (const s of sprites) {
      const sx = s.x - p.x;
      const sy = s.y - p.y;
      const transformX = invDet * (p.dirY * sx - p.dirX * sy);
      const transformY = invDet * (-p.planeY * sx + p.planeX * sy);
      if (transformY <= 0.05) continue;

      const screenX = (W / 2) * (1 + transformX / transformY);
      const fullH = Math.abs(VIEW_H / transformY);
      const sprH = fullH * s.scale;
      const sprW = sprH;
      const top = s.zCenter == null
        ? VIEW_H / 2 + fullH / 2 - sprH
        : VIEW_H / 2 + fullH * (0.5 - s.zCenter) - sprH / 2;
      const startX = Math.floor(screenX - sprW / 2);
      const endX = Math.ceil(screenX + sprW / 2);
      if (endX < 0 || startX >= W || sprW < 1) continue;

      // draw in vertical stripes, clipped by the wall z-buffer,
      // darkened with distance to match the wall fog
      let bucket = shadeBucket(transformY);
      if (!s.fullbright) {
        const light = torchLight(game.level, s.x, s.y, game.time);
        if (light > 0.5) bucket = Math.max(0, bucket - 2);
        else if (light > 0.15) bucket = Math.max(0, bucket - 1);
      }
      const img = s.fullbright ? s.img : this.shadedSprite(s.img, bucket);
      const x0 = Math.max(0, startX);
      const x1 = Math.min(W - 1, endX);
      let runStart = -1;
      for (let x = x0; x <= x1 + 1; x++) {
        const visible = x <= x1 && transformY < this.zbuffer[x];
        if (visible && runStart < 0) runStart = x;
        if (!visible && runStart >= 0) {
          const texX0 = ((runStart - startX) / sprW) * 64;
          const texX1 = ((x - startX) / sprW) * 64;
          g.drawImage(img, texX0, 0, Math.max(0.01, texX1 - texX0), 64, runStart, top, x - runStart, sprH);
          this.renderTransparentWalls(runStart, x - 1, transformY);
          runStart = -1;
        }
      }
    }
  }

  renderTorchGlows(game) {
    const g = this.ctx;
    const p = game.player;
    const invDet = 1 / (p.planeX * p.dirY - p.dirX * p.planeY);
    g.save();
    g.beginPath();
    g.rect(0, 0, W, VIEW_H);
    g.clip();
    g.globalCompositeOperation = "screen";
    for (const t of game.level.torches) {
      const sx = t.x - p.x;
      const sy = t.y - p.y;
      const transformX = invDet * (p.dirY * sx - p.dirX * sy);
      const transformY = invDet * (-p.planeY * sx + p.planeX * sy);
      if (transformY <= 0.08) continue;
      const screenX = (W / 2) * (1 + transformX / transformY);
      if (screenX < -180 || screenX > W + 180) continue;
      const zi = Math.max(0, Math.min(W - 1, Math.round(screenX)));
      if (transformY > this.zbuffer[zi] + 0.15) continue;
      const fullH = VIEW_H / transformY;
      const screenY = VIEW_H / 2 + fullH * (0.5 - 0.72);
      const radius = Math.max(16, Math.min(150, fullH * 0.62));
      const flicker = torchFlicker(game.time, t.phase);
      const grad = g.createRadialGradient(screenX, screenY, 0, screenX, screenY, radius);
      grad.addColorStop(0, `rgba(255,174,55,${0.2 * flicker})`);
      grad.addColorStop(0.28, `rgba(255,112,22,${0.1 * flicker})`);
      grad.addColorStop(1, "rgba(255,80,0,0)");
      g.fillStyle = grad;
      g.fillRect(screenX - radius, screenY - radius, radius * 2, radius * 2);
    }
    g.restore();
  }

  renderWeapon(game) {
    const g = this.ctx;
    const p = game.player;
    const frames = this.assets.weapons[p.weapon];
    const img = frames[p.weaponFrame] ?? frames[0];
    const size = 300;
    const bobX = Math.sin(p.bobPhase) * 10 * p.bobAmount;
    const bobY = Math.abs(Math.cos(p.bobPhase)) * 8 * p.bobAmount + 4;
    g.drawImage(img, W / 2 - size / 2 + bobX, VIEW_H - size + 28 + bobY, size, size);

    // crosshair
    g.fillStyle = "rgba(255,255,255,0.6)";
    g.fillRect(W / 2 - 1, VIEW_H / 2 - 1, 2, 2);
  }

  renderFlashes(game) {
    const g = this.ctx;
    if (game.portalFlash > 0) {
      g.fillStyle = `rgba(150,90,230,${Math.min(0.55, game.portalFlash)})`;
      g.fillRect(0, 0, W, VIEW_H);
    }
    if (game.damageFlash > 0) {
      g.fillStyle = `rgba(190,0,0,${Math.min(0.55, game.damageFlash)})`;
      g.fillRect(0, 0, W, VIEW_H);
    }
    if (game.pickupFlash > 0) {
      g.fillStyle = `rgba(230,200,60,${Math.min(0.3, game.pickupFlash)})`;
      g.fillRect(0, 0, W, VIEW_H);
    }
    if (game.message && game.messageTimer > 0) {
      g.font = "bold 15px Georgia, serif";
      g.textAlign = "center";
      const a = Math.min(1, game.messageTimer);
      g.fillStyle = `rgba(12,8,4,${0.6 * a})`;
      const tw = g.measureText(game.message).width;
      g.fillRect(W / 2 - tw / 2 - 12, 30, tw + 24, 26);
      g.fillStyle = `rgba(201,162,74,${0.7 * a})`;
      g.fillRect(W / 2 - tw / 2 - 12, 30, tw + 24, 1);
      g.fillRect(W / 2 - tw / 2 - 12, 55, tw + 24, 1);
      g.fillStyle = `rgba(235,215,140,${a})`;
      g.fillText(game.message, W / 2, 48);
    }
  }

  // ------------------------------------------------------------- automap

  renderMap(game) {
    const g = this.ctx;
    const { level } = game;
    const cs = Math.min(8, Math.floor((VIEW_H - 40) / level.h));
    const mw = level.w * cs;
    const mh = level.h * cs;
    const ox = W / 2 - mw / 2;
    const oy = (VIEW_H - mh) / 2;
    g.fillStyle = "rgba(16,10,4,0.8)";
    g.fillRect(ox - 10, oy - 10, mw + 20, mh + 20);
    g.fillStyle = "#c9a24a";
    g.fillRect(ox - 10, oy - 10, mw + 20, 1);
    g.fillRect(ox - 10, oy + mh + 9, mw + 20, 1);
    for (let y = 0; y < level.h; y++) {
      for (let x = 0; x < level.w; x++) {
        if (!game.visited[y * level.w + x]) continue;
        const cell = level.grid[y * level.w + x];
        let col = null;
        if (cell === ".") col = "#241c12";
        else if (cell === "D") col = "#8a5a28";
        else if (cell === "B") col = "#59636a";
        else if (cell === "R") col = "#76828a";
        else if (cell === "X") col = "#c9a24a";
        // secret walls and unpulled levers masquerade as plain wall on the
        // map; once worked they show up in door brown / lever gold
        else if (cell === "Z") col = (game.doorAt(x, y)?.open ?? 0) > 0.05 ? "#8a5a28" : "#655743";
        else if (cell === "L") col = game.leverPulled(x, y) ? "#c9a24a" : "#655743";
        else if (cell === "~") col = "#8a4fc0";
        else if (cell === "N") col = "#57493a";
        else col = "#655743";
        g.fillStyle = col;
        g.fillRect(ox + x * cs, oy + y * cs, cs - 0.5, cs - 0.5);
      }
    }
    // traps the player has set off
    for (const t of game.traps) {
      if (!t.seen) continue;
      g.fillStyle = "#8c2020";
      g.fillRect(ox + t.x * cs, oy + t.y * cs, cs - 0.5, cs - 0.5);
    }
    // player arrow
    const p = game.player;
    g.save();
    g.translate(ox + p.x * cs, oy + p.y * cs);
    g.rotate(Math.atan2(p.dirY, p.dirX));
    g.fillStyle = "#ffd23c";
    g.beginPath();
    g.moveTo(cs * 0.9, 0);
    g.lineTo(-cs * 0.5, -cs * 0.5);
    g.lineTo(-cs * 0.5, cs * 0.5);
    g.closePath();
    g.fill();
    g.restore();
    g.font = "10px Georgia, serif";
    g.textAlign = "center";
    g.fillStyle = "#a89878";
    g.fillText("MAP — TAB TO CLOSE", W / 2, oy + mh + 6);
  }

  // ------------------------------------------------------------- HUD bar

  renderHUD(game) {
    const g = this.ctx;
    const y0 = VIEW_H;
    const hudH = H - VIEW_H;

    // oak planks bound with iron
    g.fillStyle = "#332412";
    g.fillRect(0, y0, W, hudH);
    g.fillStyle = "rgba(0,0,0,0.25)";
    for (let x = 40; x < W; x += 160) g.fillRect(x, y0, 1, hudH);
    g.fillStyle = "rgba(90,60,26,0.5)";
    g.fillRect(0, y0 + hudH / 2, W, 1);
    g.fillStyle = "#23262b"; // iron rim
    g.fillRect(0, y0, W, 4);
    g.fillRect(0, H - 4, W, 4);
    g.fillStyle = "#565c66";
    g.fillRect(0, y0, W, 1);
    g.fillStyle = "#7c828e"; // rivets
    for (let x = 14; x < W; x += 52) {
      g.fillRect(x, y0 + 1, 2, 2);
      g.fillRect(x, H - 3, 2, 2);
    }

    const panel = (x, w, label, value, color = "#ead9b0") => {
      g.fillStyle = "#1a1208";
      g.fillRect(x, y0 + 8, w, hudH - 16);
      g.fillStyle = "rgba(201,162,74,0.35)";
      g.fillRect(x, y0 + 8, w, 1);
      g.fillRect(x, y0 + hudH - 9, w, 1);
      g.font = "9px Georgia, serif";
      g.textAlign = "center";
      g.fillStyle = "#b08c46";
      g.fillText(label, x + w / 2, y0 + 22);
      g.font = "bold 22px Georgia, serif";
      g.fillStyle = color;
      g.fillText(String(value), x + w / 2, y0 + 52);
    };

    panel(10, 70, "FLOOR", game.levelIndex + 1);
    panel(88, 110, "SCORE", game.score);
    panel(206, 62, "LIVES", game.lives);

    // heraldic crest that shatters as the player weakens
    const hp = game.player.health;
    const crest =
      hp <= 0 ? "dead" : hp >= 75 ? "healthy" : hp >= 50 ? "ok" : hp >= 25 ? "hurt" : "bad";
    g.fillStyle = "#1a1208";
    g.fillRect(276, y0 + 8, 62, hudH - 16);
    g.fillStyle = "rgba(201,162,74,0.35)";
    g.fillRect(276, y0 + 8, 62, 1);
    g.fillRect(276, y0 + hudH - 9, 62, 1);
    g.drawImage(this.assets.crests[crest], 283, y0 + 12, 48, 48);

    panel(346, 84, "HEALTH", `${Math.max(0, hp)}%`, hp > 25 ? "#ead9b0" : "#e05050");
    const w = game.player.weapon;
    panel(438, 70, "BOLTS", w === "dagger" ? "-" : game.player.ammo);

    // keys
    g.fillStyle = "#1a1208";
    g.fillRect(516, y0 + 8, 60, hudH - 16);
    g.fillStyle = "rgba(201,162,74,0.35)";
    g.fillRect(516, y0 + 8, 60, 1);
    g.fillRect(516, y0 + hudH - 9, 60, 1);
    g.font = "9px Georgia, serif";
    g.fillStyle = "#b08c46";
    g.fillText("KEYS", 546, y0 + 22);
    for (let i = 0; i < 3; i++) {
      g.globalAlpha = i < game.player.keys ? 1 : 0.18;
      g.drawImage(this.assets.items.key, 16, 16, 32, 40, 522 + i * 18, y0 + 26, 14, 22);
      g.globalAlpha = 1;
    }

    // weapon icon panel
    g.fillStyle = "#1a1208";
    g.fillRect(584, y0 + 8, 48, hudH - 16);
    g.fillStyle = "rgba(201,162,74,0.35)";
    g.fillRect(584, y0 + 8, 48, 1);
    g.fillRect(584, y0 + hudH - 9, 48, 1);
    g.fillStyle = "#b08c46";
    g.fillText("ARMS", 608, y0 + 22);
    const wIcon = this.assets.weaponIcons[w];
    g.drawImage(wIcon, 591, y0 + 24, 34, 34);
  }
}
