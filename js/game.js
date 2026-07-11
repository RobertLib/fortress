// In-level gameplay: player movement & combat, doors, pickups, keys/exit
// logic, line-of-sight service and the level statistics.

import { Enemy } from "./enemy.js";
import { audio } from "./audio.js";
import { WALLS } from "./level.js";

const WEAPONS = {
  dagger: { rate: 0.45, dmg: [8, 16], range: 1.7, auto: false, ammo: false, sound: "dagger" },
  crossbow: { rate: 0.42, dmg: [10, 20], range: 24, auto: false, ammo: true, sound: "crossbow" },
  arbalest: { rate: 0.12, dmg: [8, 16], range: 24, auto: true, ammo: true, sound: "arbalest" },
};

const EXTRA_LIFE_EVERY = 40000;

export class Game {
  constructor(level, levelIndex, session, persist) {
    this.level = level;
    this.levelIndex = levelIndex;
    this.session = session; // { score, lives, nextLife, totals... }

    this.player = {
      x: level.playerStart.x,
      y: level.playerStart.y,
      dirX: level.playerStart.dx,
      dirY: level.playerStart.dy,
      planeX: -level.playerStart.dy * 0.66,
      planeY: level.playerStart.dx * 0.66,
      health: persist?.health ?? 100,
      ammo: persist?.ammo ?? 16,
      weapon: persist?.weapon ?? "crossbow",
      hasArbalest: persist?.hasArbalest ?? false,
      keys: 0,
      weaponFrame: 0,
      bobPhase: 0,
      bobAmount: 0,
      speedNow: 0,
      radius: 0.22,
    };

    this.items = level.items.map((i) => ({ ...i }));
    this.enemies = level.enemies.map((s) => new Enemy(s));
    this.doors = level.doors.map((d) => ({ ...d, open: 0, state: "closed", timer: 0 }));
    this.doorMap = new Map(this.doors.map((d) => [d.y * level.w + d.x, d]));
    this.levers = level.levers.map((l) => ({ ...l, pulled: false }));
    this.leverMap = new Map(this.levers.map((l) => [l.y * level.w + l.x, l]));
    this.traps = level.traps.map((t) => ({ ...t, state: "armed", timer: 0, shots: 0, seen: false }));
    this.projectiles = [];

    this.visited = new Uint8Array(level.w * level.h);
    this.time = 0;
    this.kills = 0;
    this.treasure = 0;
    this.secretsFound = 0;
    this.totalEnemies = this.enemies.length;
    this.totalTreasure = level.treasureCount;
    this.totalSecrets = level.secretCount;

    this.damageFlash = 0;
    this.pickupFlash = 0;
    this.message = "";
    this.messageTimer = 0;
    this.fireTimer = 0;
    this.fireHeld = false;
    this.weaponAnimTimer = 0;
    this.playerDead = false;
    this.completed = false;
    this.showMap = false;
  }

  get score() {
    return this.session.score;
  }
  get lives() {
    return this.session.lives;
  }

  say(text, secs = 2.2) {
    this.message = text;
    this.messageTimer = secs;
  }

  addScore(n) {
    this.session.score += n;
    while (this.session.score >= this.session.nextLife) {
      this.session.nextLife += EXTRA_LIFE_EVERY;
      this.session.lives++;
      audio.play("extraLife");
      this.say("EXTRA LIFE!");
    }
  }

  // --------------------------------------------------------------- update

  update(dt, input) {
    if (this.playerDead || this.completed) {
      this.decayFx(dt);
      return;
    }
    this.time += dt;
    this.updateDoors(dt);
    this.updatePlayer(dt, input);
    this.updateWeapon(dt, input);
    this.updatePickups();
    this.updateTraps(dt);
    for (const e of this.enemies) e.update(this, dt);
    this.updateProjectiles(dt);
    this.markVisited();
    this.decayFx(dt);
  }

  decayFx(dt) {
    this.damageFlash = Math.max(0, this.damageFlash - dt * 1.6);
    this.pickupFlash = Math.max(0, this.pickupFlash - dt * 2.5);
    this.messageTimer = Math.max(0, this.messageTimer - dt);
  }

  markVisited() {
    const { w, h } = this.level;
    const px = Math.floor(this.player.x);
    const py = Math.floor(this.player.y);
    const R = 4;
    for (let y = Math.max(0, py - R); y <= Math.min(h - 1, py + R); y++) {
      for (let x = Math.max(0, px - R); x <= Math.min(w - 1, px + R); x++) {
        this.visited[y * w + x] = 1;
      }
    }
  }

  // --------------------------------------------------------------- player

  rotatePlayer(angle) {
    const p = this.player;
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    const dx = p.dirX * cos - p.dirY * sin;
    p.dirY = p.dirX * sin + p.dirY * cos;
    p.dirX = dx;
    const px = p.planeX * cos - p.planeY * sin;
    p.planeY = p.planeX * sin + p.planeY * cos;
    p.planeX = px;
  }

  updatePlayer(dt, input) {
    const p = this.player;
    const run = input.run ? 1.55 : 1;
    const speed = 3.4 * run * dt;
    const rotSpeed = 2.7 * dt;

    if (input.turnLeft) this.rotatePlayer(-rotSpeed);
    if (input.turnRight) this.rotatePlayer(rotSpeed);
    if (input.mouseDX) {
      this.rotatePlayer(input.mouseDX * 0.0026);
      input.mouseDX = 0;
    }

    let mx = 0;
    let my = 0;
    if (input.forward) {
      mx += p.dirX;
      my += p.dirY;
    }
    if (input.back) {
      mx -= p.dirX;
      my -= p.dirY;
    }
    if (input.strafeLeft) {
      mx += p.dirY;
      my += -p.dirX;
    }
    if (input.strafeRight) {
      mx += -p.dirY;
      my += p.dirX;
    }
    const len = Math.hypot(mx, my);
    if (len > 0.001) {
      mx /= len;
      my /= len;
      const nx = p.x + mx * speed;
      const ny = p.y + my * speed;
      if (!this.isBlocked(nx, p.y, p.radius, null)) p.x = nx;
      if (!this.isBlocked(p.x, ny, p.radius, null)) p.y = ny;
      p.speedNow = (3.4 * run * len) | 0;
      p.bobPhase += dt * 9 * run;
      p.bobAmount = Math.min(1, p.bobAmount + dt * 6);
    } else {
      p.speedNow = 0;
      p.bobAmount = Math.max(0, p.bobAmount - dt * 6);
    }

    if (input.use) {
      input.use = false;
      this.useAction();
    }
    if (input.weapon && WEAPONS[input.weapon]) {
      if (input.weapon !== "arbalest" || p.hasArbalest) {
        if (p.weapon !== input.weapon) {
          p.weapon = input.weapon;
          audio.play("blip");
        }
      }
      input.weapon = null;
    }
  }

  useAction() {
    const p = this.player;
    for (const t of [0.6, 1.1, 1.6]) {
      const cx = Math.floor(p.x + p.dirX * t);
      const cy = Math.floor(p.y + p.dirY * t);
      const cell = this.cellAt(cx, cy);
      if (cell === ".") continue;
      if (cell === "D" || cell === "R") {
        const door = this.doorAt(cx, cy);
        if (door) this.openDoor(door, true);
        return;
      }
      if (cell === "X") {
        if (p.keys >= 3) {
          this.completed = true;
          audio.play("levelDone");
        } else {
          audio.play("locked");
          this.say(`THE GATE IS LOCKED — ${3 - p.keys} KEY${3 - p.keys > 1 ? "S" : ""} MISSING`);
        }
        return;
      }
      if (cell === "L") {
        this.pullLever(cx, cy);
        return;
      }
      return; // plain wall (secret walls included — only their lever moves them)
    }
  }

  pullLever(x, y) {
    const lever = this.leverMap.get(y * this.level.w + x);
    if (!lever) return;
    // the handle sits on one face only — from any other side this is
    // just blank masonry under the player's hand
    const facing = (this.player.x - (x + 0.5)) * lever.dx + (this.player.y - (y + 0.5)) * lever.dy;
    if (facing < 0.3) return;
    if (lever.pulled) {
      audio.play("blip");
      return;
    }
    lever.pulled = true;
    audio.play("lever");
    let opened = 0;
    for (const key of lever.doors) {
      const door = this.doorMap.get(key);
      if (door && door.state === "closed") {
        door.state = "opening";
        audio.playAt("secret", door.x + 0.5, door.y + 0.5, this.player);
        opened++;
      }
    }
    if (opened) {
      this.secretsFound++;
      this.session.totalSecretsFound++;
      this.addScore(500);
      this.say("A SECRET PASSAGE GRINDS OPEN!", 3);
    }
  }

  leverAt(x, y) {
    return this.leverMap.get(y * this.level.w + x) ?? null;
  }

  leverPulled(x, y) {
    return this.leverAt(x, y)?.pulled ?? false;
  }

  // --------------------------------------------------------------- combat

  updateWeapon(dt, input) {
    const p = this.player;
    this.fireTimer = Math.max(0, this.fireTimer - dt);
    this.weaponAnimTimer = Math.max(0, this.weaponAnimTimer - dt);
    p.weaponFrame = this.weaponAnimTimer > 0 ? (this.weaponAnimTimer > 0.08 ? 1 : 2) : 0;

    const spec = WEAPONS[p.weapon];
    const wantFire = input.fire && (spec.auto || !this.fireHeld);
    this.fireHeld = input.fire;
    if (!wantFire || this.fireTimer > 0) return;

    if (spec.ammo && p.ammo <= 0) {
      audio.play("empty");
      this.fireTimer = 0.3;
      if (p.hasArbalest || p.weapon === "crossbow") this.say("OUT OF BOLTS — DAGGER! (KEY 1)");
      return;
    }

    this.fireTimer = spec.rate;
    this.weaponAnimTimer = 0.16;
    if (spec.ammo) p.ammo--;
    audio.play(spec.sound, { volume: 0.9 });
    this.alertNearby(9);

    // hitscan: nearest live enemy close enough to the view axis
    let best = null;
    let bestDist = spec.range;
    for (const e of this.enemies) {
      if (e.dead) continue;
      const dx = e.x - p.x;
      const dy = e.y - p.y;
      const dist = Math.hypot(dx, dy);
      if (dist > spec.range || dist >= bestDist) continue;
      const forward = dx * p.dirX + dy * p.dirY;
      if (forward <= 0) continue;
      const side = Math.abs(dx * -p.dirY + dy * p.dirX);
      if (side > e.radius + dist * 0.06) continue;
      if (!this.lineOfSight(p.x, p.y, e.x, e.y)) continue;
      best = e;
      bestDist = dist;
    }
    if (best) {
      const [lo, hi] = spec.dmg;
      const falloff = bestDist < 4 ? 1 : bestDist < 10 ? 0.75 : 0.55;
      best.hit(this, Math.round((lo + Math.random() * (hi - lo)) * falloff));
    }
  }

  alertNearby(radius) {
    for (const e of this.enemies) {
      if (e.dead) continue;
      const dist = Math.hypot(e.x - this.player.x, e.y - this.player.y);
      if (dist < radius && (dist < 4 || this.lineOfSight(e.x, e.y, this.player.x, this.player.y))) {
        e.wake(this, true);
      }
    }
  }

  hurtPlayer(dmg) {
    const p = this.player;
    if (this.playerDead || this.completed) return;
    p.health -= dmg;
    this.damageFlash = Math.min(0.75, 0.25 + dmg / 40);
    if (p.health <= 0) {
      p.health = 0;
      this.playerDead = true;
      audio.play("playerDeath");
    } else {
      audio.play("playerPain", { volume: 0.8 });
    }
  }

  onEnemyKilled(e) {
    this.kills++;
    this.session.totalKills++;
    this.addScore(e.stats.score);
    if (Math.random() < e.stats.drop) {
      this.items.push({ x: e.x, y: e.y, kind: "boltsDrop" });
    }
  }

  // --------------------------------------------------------------- traps

  updateTraps(dt) {
    const p = this.player;
    for (const t of this.traps) {
      const onPlate = Math.floor(p.x) === t.x && Math.floor(p.y) === t.y;
      switch (t.state) {
        case "armed":
          if (onPlate) {
            t.state = "triggered";
            t.timer = 0.35; // a heartbeat to jump off before the volley
            t.seen = true;
            audio.playAt("trapClick", t.x + 0.5, t.y + 0.5, p);
            this.say("* CLICK *", 1);
          }
          break;
        case "triggered":
          t.timer -= dt;
          if (t.timer <= 0) {
            t.state = "firing";
            t.shots = 3;
            t.timer = 0;
          }
          break;
        case "firing":
          t.timer -= dt;
          if (t.timer <= 0) {
            this.projectiles.push({ x: t.originX, y: t.originY, dirX: t.dirX, dirY: t.dirY, dist: 0 });
            audio.playAt("enemyShot", t.originX, t.originY, p, { rate: 1.25, volume: 0.9 });
            t.timer = 0.16;
            if (--t.shots <= 0) {
              t.state = "cooldown";
              t.timer = 2.5;
            }
          }
          break;
        case "cooldown":
          t.timer -= dt;
          if (t.timer <= 0 && !onPlate) t.state = "armed";
          break;
      }
    }
  }

  updateProjectiles(dt) {
    const p = this.player;
    const step = (9 * dt) / 3; // substeps so fast arrows can't skip a target
    for (let i = this.projectiles.length - 1; i >= 0; i--) {
      const a = this.projectiles[i];
      let dead = false;
      for (let s = 0; s < 3 && !dead; s++) {
        a.x += a.dirX * step;
        a.y += a.dirY * step;
        a.dist += step;
        const cx = Math.floor(a.x);
        const cy = Math.floor(a.y);
        const cell = this.cellAt(cx, cy);
        if (cell === "B" || cell === "R") continue;
        if (cell !== "." && !((cell === "D" || cell === "R" || cell === "Z") && this.doorOpenAmount(cx, cy) > 0.85)) {
          audio.playAt("arrowHit", a.x, a.y, p, { volume: 0.7 });
          dead = true;
          break;
        }
        if (!this.playerDead && Math.hypot(p.x - a.x, p.y - a.y) < p.radius + 0.12) {
          this.hurtPlayer(10 + Math.floor(Math.random() * 9));
          dead = true;
          break;
        }
        for (const e of this.enemies) {
          if (e.dead) continue;
          if (Math.hypot(e.x - a.x, e.y - a.y) < e.radius + 0.05) {
            e.hit(this, 14 + Math.floor(Math.random() * 10));
            dead = true;
            break;
          }
        }
        if (a.dist > 14) dead = true;
      }
      if (dead) this.projectiles.splice(i, 1);
    }
  }

  // --------------------------------------------------------------- doors

  cellAt(x, y) {
    const { grid, w, h } = this.level;
    if (x < 0 || y < 0 || x >= w || y >= h) return "1";
    return grid[y * w + x];
  }

  doorAt(x, y) {
    return this.doorMap.get(y * this.level.w + x) ?? null;
  }

  doorOpenAmount(x, y) {
    return this.doorAt(x, y)?.open ?? 0;
  }

  openDoor(door, byPlayer) {
    if (door.kind === "Z") return; // secret walls only answer to their lever
    if (door.state === "closed" || door.state === "closing") {
      door.state = "opening";
      audio.playAt("door", door.x + 0.5, door.y + 0.5, this.player, { volume: byPlayer ? 1 : 0.8 });
    }
  }

  updateDoors(dt) {
    for (const d of this.doors) {
      if (d.state === "opening") {
        d.open += dt * (d.kind === "Z" ? 0.55 : 1.6); // stone grinds slowly
        if (d.open >= 1) {
          d.open = 1;
          d.state = "open";
          d.timer = 4;
        }
      } else if (d.state === "open") {
        if (d.kind === "Z") continue; // a revealed passage never seals again
        d.timer -= dt;
        if (d.timer <= 0) {
          if (this.doorwayOccupied(d)) {
            d.timer = 1;
          } else {
            d.state = "closing";
            audio.playAt("door", d.x + 0.5, d.y + 0.5, this.player, { volume: 0.7, rate: 0.85 });
          }
        }
      } else if (d.state === "closing") {
        d.open -= dt * 1.6;
        if (d.open <= 0) {
          d.open = 0;
          d.state = "closed";
        }
      }
    }
  }

  doorwayOccupied(d) {
    const inCell = (ex, ey, r) =>
      ex + r > d.x && ex - r < d.x + 1 && ey + r > d.y && ey - r < d.y + 1;
    if (inCell(this.player.x, this.player.y, this.player.radius + 0.1)) return true;
    for (const e of this.enemies) {
      if (!e.dead && inCell(e.x, e.y, e.radius + 0.1)) return true;
    }
    return false;
  }

  // --------------------------------------------------------------- world

  isBlocked(x, y, r, self) {
    const minX = Math.floor(x - r);
    const maxX = Math.floor(x + r);
    const minY = Math.floor(y - r);
    const maxY = Math.floor(y + r);
    for (let cy = minY; cy <= maxY; cy++) {
      for (let cx = minX; cx <= maxX; cx++) {
        const cell = this.cellAt(cx, cy);
        if (cell === ".") continue;
        if ((cell === "D" || cell === "R" || cell === "Z") && this.doorOpenAmount(cx, cy) > 0.85) continue;
        return true;
      }
    }
    // entities block each other — except bat swarms, which fly right through
    // (and clear over the furniture too)
    if (self?.stats?.swarm) return false;
    for (const d of this.level.decorations) {
      if (!d.radius) continue;
      if ((d.x - x) ** 2 + (d.y - y) ** 2 < (r + d.radius) ** 2) return true;
    }
    for (const e of this.enemies) {
      if (e === self || e.dead || e.stats.swarm) continue;
      const d = Math.hypot(e.x - x, e.y - y);
      if (d < r + e.radius) return true;
    }
    if (self) {
      const d = Math.hypot(this.player.x - x, this.player.y - y);
      if (d < r + this.player.radius + 0.15) return true;
    }
    return false;
  }

  lineOfSight(x0, y0, x1, y1) {
    // voxel traversal; solid walls and mostly-closed wooden doors block sight.
    // Bars are physical obstacles, but intentionally do not block vision/fire.
    let cx = Math.floor(x0);
    let cy = Math.floor(y0);
    const tx = Math.floor(x1);
    const ty = Math.floor(y1);
    const dx = x1 - x0;
    const dy = y1 - y0;
    const stepX = dx > 0 ? 1 : -1;
    const stepY = dy > 0 ? 1 : -1;
    const tDeltaX = dx !== 0 ? Math.abs(1 / dx) : Infinity;
    const tDeltaY = dy !== 0 ? Math.abs(1 / dy) : Infinity;
    let tMaxX = dx !== 0 ? (dx > 0 ? cx + 1 - x0 : x0 - cx) * tDeltaX : Infinity;
    let tMaxY = dy !== 0 ? (dy > 0 ? cy + 1 - y0 : y0 - cy) * tDeltaY : Infinity;

    for (let i = 0; i < 128; i++) {
      if (cx === tx && cy === ty) return true;
      if (tMaxX < tMaxY) {
        tMaxX += tDeltaX;
        cx += stepX;
      } else {
        tMaxY += tDeltaY;
        cy += stepY;
      }
      const cell = this.cellAt(cx, cy);
      if (cell === ".") continue;
      if (cell === "B" || cell === "R") continue;
      if ((cell === "D" || cell === "Z") && this.doorOpenAmount(cx, cy) > 0.6) continue;
      if (cx === tx && cy === ty) return true;
      return false;
    }
    return false;
  }

  // --------------------------------------------------------------- items

  updatePickups() {
    const p = this.player;
    for (let i = this.items.length - 1; i >= 0; i--) {
      const it = this.items[i];
      if (Math.hypot(it.x - p.x, it.y - p.y) > 0.55) continue;
      if (!this.applyPickup(it)) continue;
      this.items.splice(i, 1);
      this.pickupFlash = 0.35;
    }
  }

  applyPickup(it) {
    const p = this.player;
    switch (it.kind) {
      case "key":
        p.keys++;
        this.addScore(200);
        audio.play("pickupKey");
        if (p.keys >= 3) {
          this.say("ALL 3 KEYS FOUND — THE GATE IS OPEN!", 3);
          audio.play("exitOpen");
        } else {
          this.say(`KEY FOUND (${p.keys}/3)`);
        }
        return true;
      case "potion":
        if (p.health >= 100) return false;
        p.health = Math.min(100, p.health + 25);
        audio.play("pickupHealth");
        return true;
      case "bread":
        if (p.health >= 100) return false;
        p.health = Math.min(100, p.health + 10);
        audio.play("pickup");
        return true;
      case "bolts":
        if (p.ammo >= 99) return false;
        p.ammo = Math.min(99, p.ammo + 8);
        audio.play("pickupAmmo");
        return true;
      case "boltsDrop":
        if (p.ammo >= 99) return false;
        p.ammo = Math.min(99, p.ammo + 4);
        audio.play("pickupAmmo");
        return true;
      case "treasure":
        this.treasure++;
        this.session.totalTreasure++;
        this.addScore(500);
        audio.play("pickup", { rate: 1.3 });
        return true;
      case "arbalest":
        p.hasArbalest = true;
        p.ammo = Math.min(99, p.ammo + 20);
        p.weapon = "arbalest";
        audio.play("pickupWeapon");
        this.say("REPEATING CROSSBOW!");
        return true;
    }
    return false;
  }
}
