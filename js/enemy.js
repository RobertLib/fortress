// Enemy AI: patrol (or stand guard) -> alert -> chase -> stop & shoot,
// with pain staggers and a small death animation. Rendered as billboards.
// The bat swarm is the odd one out: it flies (drawn head-high, passes
// through other actors), swoops at the player, bites and veers away.
// War hounds are melee runners: one leashed to a handler heels to him,
// barks him awake when it spots the player (and springs up when HE joins
// a fight), charges in to bite, and trots back to the master's side when
// the trail goes cold. A masterless hound guards its ground the same way.

import { audio } from "./audio.js";

const TYPES = {
  guard: { hp: 25, speed: 1.7, range: 8, aimTime: 0.42, cooldown: [0.7, 1.5], dmg: 14, score: 100, drop: 0.6 },
  knight: { hp: 60, speed: 1.9, range: 9, aimTime: 0.36, cooldown: [0.5, 1.2], dmg: 21, score: 500, drop: 0.8 },
  captain: { hp: 45, speed: 2.7, range: 9, aimTime: 0.26, cooldown: [0.4, 1.0], dmg: 17, score: 400, drop: 0.5 },
  bat: {
    hp: 16, speed: 3.1, range: 0.8, cooldown: [0.9, 1.6], dmg: 7, score: 150, drop: 0,
    swarm: true, radius: 0.25, scale: 0.6,
    alertSound: "batAlert", painSound: "batSqueak", deathSound: "batDeath", biteSound: "batBite",
  },
  dog: {
    hp: 18, speed: 4.1, range: 0.95, cooldown: [0.7, 1.3], dmg: 10, score: 200, drop: 0,
    melee: true, radius: 0.24, scale: 0.62,
    alertSound: "dogBark", painSound: "dogPain", deathSound: "dogDeath", biteSound: "dogBite",
  },
};

const CARDINALS = [
  [1, 0],
  [-1, 0],
  [0, 1],
  [0, -1],
];

export class Enemy {
  constructor(spawn) {
    this.x = spawn.x;
    this.y = spawn.y;
    this.type = spawn.type;
    this.stats = TYPES[spawn.type];
    this.hp = this.stats.hp;
    this.radius = this.stats.radius ?? 0.3;

    this.state = spawn.patrol ? "patrol" : "stand";
    // hounds: index of the handler in the enemies array (resolved by Game
    // into this.master once every enemy exists), and the spot to fall back
    // to when there is no handler to return to
    this.masterIndex = spawn.master ?? null;
    this.master = null;
    if (this.masterIndex != null) this.state = "follow";
    this.homeX = spawn.x;
    this.homeY = spawn.y;
    this.spawnPatrol = !!spawn.patrol;
    this.lostTimer = 0;
    this.growlTimer = 0;
    this.lastSeenX = null;
    this.lastSeenY = null;
    const [dx, dy] = CARDINALS[Math.floor(Math.random() * 4)];
    this.dirX = dx;
    this.dirY = dy;
    this.faceX = dx; // heading the hound was last moving in (drives its view)
    this.faceY = dy;
    this.timer = 0;
    this.animTime = Math.random() * 10;
    this.moving = false;
    this.attackCooldown = 0;
    this.reaction = 0;
    this.alive = true;
    this.wobble = Math.random() * Math.PI * 2; // flight-path phase (bats)
    this.flutterTimer = 0;
    this.flyHeight = undefined; // sprite zCenter; stays undefined for footmen
  }

  get dead() {
    return this.state === "dead" || this.state === "dying";
  }

  update(game, dt) {
    this.animTime += dt;
    this.moving = false;
    const p = game.player;

    if (this.stats.swarm && !this.dead) {
      // roosting swarms hang under the ceiling; airborne ones bob a little
      this.flyHeight = this.state === "stand"
        ? 0.8
        : 0.54 + Math.sin(this.animTime * 3.2 + this.wobble) * 0.05;
      if (this.state === "chase" || this.state === "swoop") this.flutterWings(game, dt);
    }

    switch (this.state) {
      case "dead":
        return;
      case "dying":
        this.timer -= dt;
        if (this.stats.swarm) {
          // the swarm tumbles out of the air
          this.flyHeight = Math.max(this.stats.scale / 2, (this.flyHeight ?? 0.5) - dt * 1.6);
        }
        if (this.timer <= 0) this.state = "dead";
        return;
      case "pain":
        this.timer -= dt;
        if (this.timer <= 0) this.state = "chase";
        return;

      case "stand":
      case "patrol": {
        if (this.state === "patrol") {
          if (this.stats.swarm) {
            // aimless fluttering: drift on a free angle, re-pick it often
            this.timer -= dt;
            const ok = this.tryMove(game, this.dirX, this.dirY, this.stats.speed * 0.45 * dt);
            if (this.timer <= 0 || !ok) {
              const a = Math.random() * Math.PI * 2;
              this.dirX = Math.cos(a);
              this.dirY = Math.sin(a);
              this.timer = 0.6 + Math.random() * 0.9;
            }
            this.moving = true;
          } else if (!this.tryMove(game, this.dirX, this.dirY, this.stats.speed * 0.55 * dt)) {
            const [dx, dy] = CARDINALS[Math.floor(Math.random() * 4)];
            this.dirX = dx;
            this.dirY = dy;
          } else {
            this.moving = true;
          }
        }
        if (this.stats.melee) {
          this.watchForPlayer(game, dt);
        } else if (this.canSeePlayer(game)) {
          this.reaction += dt;
          if (this.reaction > 0.25 + Math.random() * 0.3) this.wake(game);
        } else {
          this.reaction = Math.max(0, this.reaction - dt);
        }
        return;
      }

      case "follow": {
        // at heel: trail the handler, sit tight when close
        const m = this.master;
        if (!m || m.dead) {
          // the handler has fallen — the hound guards the spot instead
          if (m) {
            this.homeX = m.x;
            this.homeY = m.y;
          }
          this.state = "stand";
          return;
        }
        const d = Math.hypot(m.x - this.x, m.y - this.y);
        if (d > 1.4) {
          const step = this.stats.speed * (d > 3 ? 0.85 : 0.5) * dt;
          this.moveToward(game, m.x, m.y, step);
        }
        this.watchForPlayer(game, dt);
        return;
      }

      case "return": {
        // the trail went cold: trot back to the handler (or the guard post),
        // ready to spring again the moment the player shows himself
        const m = this.master && !this.master.dead ? this.master : null;
        const tx = m ? m.x : this.homeX;
        const ty = m ? m.y : this.homeY;
        const d = Math.hypot(tx - this.x, ty - this.y);
        if (d < 1.2) {
          this.state = m ? "follow" : this.spawnPatrol ? "patrol" : "stand";
          this.reaction = 0;
          return;
        }
        const nx = (tx - this.x) / d;
        const ny = (ty - this.y) / d;
        if (!this.moveToward(game, tx, ty, this.stats.speed * 0.8 * dt)) {
          this.state = m ? "follow" : "stand"; // wedged: settle where it stands
        }
        // noses doors open on the way home
        const door = game.doorAt(Math.floor(this.x + nx * 0.8), Math.floor(this.y + ny * 0.8));
        if (door) game.openDoor(door, false);
        this.watchForPlayer(game, dt);
        return;
      }

      case "chase": {
        this.attackCooldown -= dt;
        const dist = Math.hypot(p.x - this.x, p.y - this.y);
        if (this.stats.swarm) {
          // swoop straight through the player on a weaving path
          const nx = (p.x - this.x) / (dist || 1);
          const ny = (p.y - this.y) / (dist || 1);
          const wob = Math.sin(this.animTime * 6.5 + this.wobble) * 0.7;
          this.tryMove(game, nx - ny * wob, ny + nx * wob, this.stats.speed * dt);
          this.moving = true;
          if (dist < this.stats.range && this.attackCooldown <= 0) this.bite(game);
          const door = game.doorAt(Math.floor(this.x + nx * 0.8), Math.floor(this.y + ny * 0.8));
          if (door) game.openDoor(door, false);
          return;
        }
        if (this.stats.melee) {
          // run down the player — or the spot he was last seen at; once the
          // trail stays cold too long, give up the hunt and head home
          const seen = game.lineOfSight(this.x, this.y, p.x, p.y);
          if (seen) {
            this.lastSeenX = p.x;
            this.lastSeenY = p.y;
            this.lostTimer = 0;
          } else {
            this.lostTimer += dt;
            if (this.lostTimer > 3.5) {
              this.lostTimer = 0;
              this.state = "return";
              return;
            }
          }
          const tx = this.lastSeenX ?? p.x;
          const ty = this.lastSeenY ?? p.y;
          const d = Math.hypot(tx - this.x, ty - this.y);
          const nx = (tx - this.x) / (d || 1);
          const ny = (ty - this.y) / (d || 1);
          if (d > 0.3) this.moveToward(game, tx, ty, this.stats.speed * dt);
          const door = game.doorAt(Math.floor(this.x + nx * 0.8), Math.floor(this.y + ny * 0.8));
          if (door) game.openDoor(door, false);
          if (seen && dist < this.stats.range && this.attackCooldown <= 0) {
            this.bite(game);
            return;
          }
          this.growlTimer -= dt;
          if (this.growlTimer <= 0) {
            this.growlTimer = 1.1 + Math.random() * 1.2;
            audio.playAt("dogGrowl", this.x, this.y, p, { volume: 0.7, rate: 0.9 + Math.random() * 0.3 });
          }
          return;
        }
        const los = game.lineOfSight(this.x, this.y, p.x, p.y);
        if (los && dist < this.stats.range && this.attackCooldown <= 0 && dist > 0.7) {
          this.state = "aim";
          this.timer = this.stats.aimTime;
          return;
        }
        // advance toward the player; if too close, hold position
        if (dist > 1.1) {
          const nx = (p.x - this.x) / dist;
          const ny = (p.y - this.y) / dist;
          const step = this.stats.speed * dt;
          if (this.tryMove(game, nx, ny, step)) {
            this.moving = true;
          } else if (this.tryMove(game, Math.sign(nx), 0, step)) {
            this.moving = true;
          } else if (this.tryMove(game, 0, Math.sign(ny), step)) {
            this.moving = true;
          }
          // chasing enemies push doors open
          const door = game.doorAt(Math.floor(this.x + nx * 0.8), Math.floor(this.y + ny * 0.8));
          if (door) game.openDoor(door, false);
        }
        return;
      }

      case "aim": {
        this.timer -= dt;
        if (this.timer <= 0) {
          this.fire(game);
          this.state = "fire";
          this.timer = 0.18;
        }
        return;
      }

      case "fire": {
        this.timer -= dt;
        if (this.timer <= 0) {
          const [a, b] = this.stats.cooldown;
          this.attackCooldown = a + Math.random() * (b - a);
          this.state = "chase";
        }
        return;
      }

      case "bite": {
        // hounds only: jaws shown for a beat before darting off
        this.timer -= dt;
        if (this.timer <= 0) {
          this.state = "swoop";
          this.timer = 0.3 + Math.random() * 0.2;
        }
        return;
      }

      case "swoop": {
        // after a bite the swarm (or hound) veers off before circling back
        this.timer -= dt;
        this.tryMove(game, this.dirX, this.dirY, this.stats.speed * 0.85 * dt);
        this.moving = true;
        if (this.timer <= 0) this.state = "chase";
        return;
      }
    }
  }

  bite(game) {
    const p = game.player;
    audio.playAt(this.stats.biteSound, this.x, this.y, p);
    game.hurtPlayer(Math.max(2, Math.round(2 + Math.random() * this.stats.dmg)));
    const [a, b] = this.stats.cooldown;
    this.attackCooldown = a + Math.random() * (b - a);
    // peel away past the player's shoulder
    const dx = this.x - p.x;
    const dy = this.y - p.y;
    const d = Math.hypot(dx, dy) || 1;
    const side = Math.random() < 0.5 ? 1 : -1;
    this.dirX = (dx / d) * 0.6 + (-dy / d) * side;
    this.dirY = (dy / d) * 0.6 + (dx / d) * side;
    if (this.stats.melee) {
      this.state = "bite";
      this.timer = 0.2;
    } else {
      this.state = "swoop";
      this.timer = 0.4 + Math.random() * 0.25;
    }
  }

  // Dog perception: quicker on the uptake than the soldiery, and a hound
  // whose handler joins a fight springs up with him even without a sighting.
  watchForPlayer(game, dt) {
    const m = this.master;
    if (m && !m.dead && (m.state === "chase" || m.state === "aim" || m.state === "fire")) {
      this.wake(game);
      return;
    }
    if (this.canSeePlayer(game)) {
      this.reaction += dt;
      if (this.reaction > 0.1 + Math.random() * 0.15) this.wake(game);
    } else {
      this.reaction = Math.max(0, this.reaction - dt);
    }
  }

  moveToward(game, tx, ty, step) {
    const nx = tx - this.x;
    const ny = ty - this.y;
    if (this.tryMove(game, nx, ny, step)) this.moving = true;
    else if (this.tryMove(game, Math.sign(nx), 0, step)) this.moving = true;
    else if (this.tryMove(game, 0, Math.sign(ny), step)) this.moving = true;
    else return false;
    return true;
  }

  flutterWings(game, dt) {
    this.flutterTimer -= dt;
    if (this.flutterTimer > 0) return;
    this.flutterTimer = 0.28 + Math.random() * 0.2;
    audio.playAt("flutter", this.x, this.y, game.player, { volume: 0.55, rate: 0.9 + Math.random() * 0.25 });
  }

  canSeePlayer(game) {
    const p = game.player;
    const dist = Math.hypot(p.x - this.x, p.y - this.y);
    if (dist > 11) return false;
    return game.lineOfSight(this.x, this.y, p.x, p.y);
  }

  wake(game, silent = false) {
    if (this.dead || this.state === "chase" || this.state === "aim" || this.state === "fire" || this.state === "swoop" || this.state === "bite") return;
    this.state = "chase";
    this.attackCooldown = 0.35 + Math.random() * 0.4;
    this.lostTimer = 0;
    this.lastSeenX = game.player.x;
    this.lastSeenY = game.player.y;
    if (!silent) audio.playAt(this.stats.alertSound ?? "alert", this.x, this.y, game.player, { rate: 0.9 + Math.random() * 0.25 });
    // a barking hound brings its handler running
    if (this.master && !this.master.dead) this.master.wake(game, true);
  }

  fire(game) {
    const p = game.player;
    if (!game.lineOfSight(this.x, this.y, p.x, p.y)) return;
    audio.playAt("enemyShot", this.x, this.y, p);
    const dist = Math.hypot(p.x - this.x, p.y - this.y);
    const evade = p.speedNow > 1.5 ? 2.5 : 0;
    const hitChance = Math.max(0.1, 1 - (dist + evade) / 14);
    if (Math.random() < hitChance) {
      const falloff = dist < 2 ? 1 : dist < 6 ? 0.7 : 0.45;
      const dmg = Math.max(3, Math.round((4 + Math.random() * this.stats.dmg) * falloff));
      game.hurtPlayer(dmg, this);
    }
  }

  hit(game, dmg) {
    if (this.dead) return;
    this.hp -= dmg;
    this.wake(game, true);
    if (this.hp <= 0) {
      this.die(game);
      return;
    }
    audio.playAt(this.stats.painSound ?? "enemyPain", this.x, this.y, game.player);
    if (Math.random() < 0.7) {
      this.state = "pain";
      this.timer = 0.22;
    }
  }

  die(game) {
    this.state = "dying";
    this.timer = 0.4;
    this.alive = false;
    audio.playAt(this.stats.deathSound ?? "enemyDeath", this.x, this.y, game.player, { rate: 0.9 + Math.random() * 0.2 });
    game.onEnemyKilled(this);
  }

  tryMove(game, nx, ny, step) {
    const len = Math.hypot(nx, ny) || 1;
    const tx = this.x + (nx / len) * step;
    const ty = this.y + (ny / len) * step;
    let moved = false;
    if (!game.isBlocked(tx, this.y, this.radius, this)) {
      this.x = tx;
      moved = true;
    }
    if (!game.isBlocked(this.x, ty, this.radius, this)) {
      this.y = ty;
      moved = true;
    }
    if (moved && this.stats.melee) {
      this.faceX = nx / len;
      this.faceY = ny / len;
    }
    return moved;
  }

  // Which of the four hound views the player sees: its heading projected
  // into camera space, bucketed the same way the arrow sprites are.
  viewFrom(p) {
    const depth = this.faceX * p.dirX + this.faceY * p.dirY;
    const lat = (this.faceX * p.planeX + this.faceY * p.planeY) / 0.66;
    if (depth < -Math.abs(lat)) return "front"; // running at the viewer
    if (depth > Math.abs(lat)) return "back"; // running away
    return lat > 0 ? "right" : "left";
  }

  sprite(assets, player) {
    const set = assets.enemySprites[this.type];
    switch (this.state) {
      case "dead":
        return set.dead;
      case "dying":
        return this.timer > 0.2 ? set.die1 : set.die2;
      case "pain":
        return set.pain;
      case "aim":
        return set.aim;
      case "fire":
      case "bite":
        return set.fire;
      default: {
        const f = Math.floor(this.animTime * (this.stats.swarm ? 11 : this.stats.melee ? 9 : 5)) % 2;
        const frame = !this.moving ? "stand" : f ? "walk1" : "walk2";
        if (this.stats.melee && player) return set[`${frame}_${this.viewFrom(player)}`];
        return set[frame];
      }
    }
  }
}
