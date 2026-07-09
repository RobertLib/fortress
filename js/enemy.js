// Enemy AI: patrol (or stand guard) -> alert -> chase -> stop & shoot,
// with pain staggers and a small death animation. Rendered as billboards.

import { audio } from "./audio.js";

const TYPES = {
  guard: { hp: 25, speed: 1.7, range: 8, aimTime: 0.42, cooldown: [0.7, 1.5], dmg: 14, score: 100, drop: 0.6 },
  knight: { hp: 60, speed: 1.9, range: 9, aimTime: 0.36, cooldown: [0.5, 1.2], dmg: 21, score: 500, drop: 0.8 },
  captain: { hp: 45, speed: 2.7, range: 9, aimTime: 0.26, cooldown: [0.4, 1.0], dmg: 17, score: 400, drop: 0.5 },
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
    this.radius = 0.3;

    this.state = spawn.patrol ? "patrol" : "stand";
    const [dx, dy] = CARDINALS[Math.floor(Math.random() * 4)];
    this.dirX = dx;
    this.dirY = dy;
    this.timer = 0;
    this.animTime = Math.random() * 10;
    this.moving = false;
    this.attackCooldown = 0;
    this.reaction = 0;
    this.alive = true;
  }

  get dead() {
    return this.state === "dead" || this.state === "dying";
  }

  update(game, dt) {
    this.animTime += dt;
    this.moving = false;
    const p = game.player;

    switch (this.state) {
      case "dead":
        return;
      case "dying":
        this.timer -= dt;
        if (this.timer <= 0) this.state = "dead";
        return;
      case "pain":
        this.timer -= dt;
        if (this.timer <= 0) this.state = "chase";
        return;

      case "stand":
      case "patrol": {
        if (this.state === "patrol") {
          if (!this.tryMove(game, this.dirX, this.dirY, this.stats.speed * 0.55 * dt)) {
            const [dx, dy] = CARDINALS[Math.floor(Math.random() * 4)];
            this.dirX = dx;
            this.dirY = dy;
          } else {
            this.moving = true;
          }
        }
        if (this.canSeePlayer(game)) {
          this.reaction += dt;
          if (this.reaction > 0.25 + Math.random() * 0.3) this.wake(game);
        } else {
          this.reaction = Math.max(0, this.reaction - dt);
        }
        return;
      }

      case "chase": {
        this.attackCooldown -= dt;
        const dist = Math.hypot(p.x - this.x, p.y - this.y);
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
    }
  }

  canSeePlayer(game) {
    const p = game.player;
    const dist = Math.hypot(p.x - this.x, p.y - this.y);
    if (dist > 11) return false;
    return game.lineOfSight(this.x, this.y, p.x, p.y);
  }

  wake(game, silent = false) {
    if (this.dead || this.state === "chase" || this.state === "aim" || this.state === "fire") return;
    this.state = "chase";
    this.attackCooldown = 0.35 + Math.random() * 0.4;
    if (!silent) audio.playAt("alert", this.x, this.y, game.player, { rate: 0.9 + Math.random() * 0.25 });
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
    audio.playAt("enemyPain", this.x, this.y, game.player);
    if (Math.random() < 0.7) {
      this.state = "pain";
      this.timer = 0.22;
    }
  }

  die(game) {
    this.state = "dying";
    this.timer = 0.4;
    this.alive = false;
    audio.playAt("enemyDeath", this.x, this.y, game.player, { rate: 0.9 + Math.random() * 0.2 });
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
    return moved;
  }

  sprite(assets) {
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
        return set.fire;
      default: {
        if (!this.moving) return set.stand;
        const f = Math.floor(this.animTime * 5) % 2;
        return f ? set.walk1 : set.walk2;
      }
    }
  }
}
