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

// distance buckets for pre-darkened sprite variants
const SPRITE_SHADES = [1, 0.78, 0.58, 0.42];
const shadeBucket = (dist) => (dist < 3.5 ? 0 : dist < 6 ? 1 : dist < 9 ? 2 : 3);

export class Renderer {
  constructor(canvas, assets) {
    this.ctx = canvas.getContext("2d");
    this.ctx.imageSmoothingEnabled = false;
    this.assets = assets;
    this.zbuffer = new Float32Array(W);
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
      if (hit.dist <= 0.0001) continue;

      const lineHeight = VIEW_H / hit.dist;
      const drawStart = VIEW_H / 2 - lineHeight / 2;
      const texSet = hit.side === 1 ? this.assets.wallsDark : this.assets.walls;
      const tex = texSet[hit.tex] ?? texSet["1"];
      let texX = Math.floor(hit.wallX * 64);
      if (texX < 0) texX = 0;
      if (texX > 63) texX = 63;
      g.drawImage(tex, texX, 0, 1, 64, x, drawStart, 1, lineHeight);

      const fog = fogAmount(hit.dist);
      if (fog > 0.02) {
        g.fillStyle = `rgba(${FOG},${fog})`;
        g.fillRect(x, drawStart, 1, lineHeight);
      }
    }

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

      if (cell === "D") {
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
          return { dist: distMid, side, tex: "D", wallX: coord - open };
        }
        continue; // door open at this spot — ray passes through
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
      return { dist, side, tex: cell, wallX };
    }
    return { dist: 1e9, side: 0, tex: "1", wallX: 0 };
  }

  renderSprites(game) {
    const g = this.ctx;
    const p = game.player;
    const sprites = [];
    for (const e of game.enemies) {
      sprites.push({ x: e.x, y: e.y, img: e.sprite(this.assets), scale: 0.82 });
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
      const bottom = VIEW_H / 2 + fullH / 2;
      const top = bottom - sprH;
      const startX = Math.floor(screenX - sprW / 2);
      const endX = Math.ceil(screenX + sprW / 2);
      if (endX < 0 || startX >= W || sprW < 1) continue;

      // draw in vertical stripes, clipped by the wall z-buffer,
      // darkened with distance to match the wall fog
      const img = this.shadedSprite(s.img, shadeBucket(transformY));
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
          runStart = -1;
        }
      }
    }
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
        else if (cell === "X") col = "#c9a24a";
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
