// FORTRESS — a medieval first-person raycasting adventure.
// Entry point: asset/level loading, input, game states and the main loop.

import { buildAssets } from "./js/textures.js";
import { loadLevels } from "./js/level.js";
import { Renderer, W, H, VIEW_H } from "./js/engine.js";
import { Game } from "./js/game.js";
import { audio } from "./js/audio.js";

const LEVEL_COUNT = 5;
const canvas = document.getElementById("screen");
const ctx = canvas.getContext("2d");

const assets = buildAssets();
const renderer = new Renderer(canvas, assets);

let levels = null;
let state = "loading"; // loading | intro | menu | controls | quit | levelstart |
//                        playing | paused | died | levelcomplete | gameover |
//                        victory | error
let errorMsg = "";
let game = null;
let session = null;
let stateTimer = 0;
let deathDelay = 0;
let clock = 0;

// ------------------------------------------------------------------ input

const input = {
  forward: false,
  back: false,
  strafeLeft: false,
  strafeRight: false,
  turnLeft: false,
  turnRight: false,
  run: false,
  strafeMod: false,
  fire: false,
  use: false,
  weapon: null,
  mouseDX: 0,
};

const KEYMAP = {
  KeyW: "forward",
  ArrowUp: "forward",
  KeyS: "back",
  ArrowDown: "back",
  KeyA: "strafeLeft",
  KeyD: "strafeRight",
  ArrowLeft: "turnLeft",
  ArrowRight: "turnRight",
  ShiftLeft: "run",
  ShiftRight: "run",
  AltLeft: "strafeMod",
  AltRight: "strafeMod",
  ControlLeft: "fire",
  ControlRight: "fire",
  KeyJ: "fire",
};

window.addEventListener("keydown", (e) => {
  if (
    ["Tab", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space", "AltLeft", "AltRight"].includes(e.code)
  ) {
    e.preventDefault();
  }
  audio.init();
  if (e.repeat) return;

  if (KEYMAP[e.code] !== undefined) input[KEYMAP[e.code]] = true;
  if (e.code === "KeyE" || e.code === "Space") input.use = true;
  if (e.code === "Digit1") input.weapon = "dagger";
  if (e.code === "Digit2") input.weapon = "crossbow";
  if (e.code === "Digit3") input.weapon = "arbalest";
  if (e.code === "KeyM") {
    audio.enabled = !audio.enabled;
    if (game) game.say(audio.enabled ? "SOUND ON" : "SOUND OFF", 1.2);
  }
  if (e.code === "KeyF") toggleFullscreen();
  if (e.code === "Tab" && game && state === "playing") game.showMap = !game.showMap;

  handleStateKey(e.code);
});

window.addEventListener("keyup", (e) => {
  if (KEYMAP[e.code] !== undefined) input[KEYMAP[e.code]] = false;
});

window.addEventListener("blur", () => {
  for (const key of Object.keys(input)) {
    if (typeof input[key] === "boolean") input[key] = false;
  }
});

canvas.addEventListener("mousedown", (e) => {
  audio.init();
  if (state === "playing") {
    if (document.pointerLockElement !== canvas) {
      canvas.requestPointerLock?.();
    }
    if (e.button === 0) input.fire = true;
  }
});
window.addEventListener("mouseup", (e) => {
  if (e.button === 0) input.fire = false;
});
window.addEventListener("mousemove", (e) => {
  if (document.pointerLockElement === canvas && state === "playing") {
    input.mouseDX += e.movementX;
  }
});

// ------------------------------------------------------------------ menus

const soundLabel = () => `SOUND: ${audio.enabled ? "ON" : "OFF"}`;
const fullscreenLabel = () => `FULLSCREEN: ${document.fullscreenElement ? "ON" : "OFF"}`;

function toggleFullscreen() {
  if (document.fullscreenElement) {
    document.exitFullscreen?.();
  } else {
    document.documentElement.requestFullscreen?.();
  }
}

let resumable = false; // a run was left via the menu and can be picked back up

const CONTINUE_ITEM = {
  label: () => "CONTINUE",
  action: () => {
    audio.play("blip");
    setState("playing");
  },
};

const MAIN_MENU = [
  {
    label: () => "NEW GAME",
    action: () => {
      audio.play("start");
      newGame();
    },
  },
  {
    label: () => "CONTROLS",
    action: () => {
      audio.play("blip");
      setState("controls");
    },
  },
  {
    label: soundLabel,
    action: () => {
      audio.enabled = !audio.enabled;
      audio.play("blip");
    },
  },
  {
    label: fullscreenLabel,
    action: () => {
      audio.play("blip");
      toggleFullscreen();
    },
  },
  {
    label: () => "QUIT GAME",
    action: () => {
      audio.play("blip");
      setState("quit");
    },
  },
];

const PAUSE_MENU = [
  {
    label: () => "RESUME",
    action: () => {
      audio.play("blip");
      setState("playing");
    },
  },
  {
    label: soundLabel,
    action: () => {
      audio.enabled = !audio.enabled;
      audio.play("blip");
    },
  },
  {
    label: fullscreenLabel,
    action: () => {
      audio.play("blip");
      toggleFullscreen();
    },
  },
  {
    label: () => "QUIT TO MENU",
    action: () => {
      audio.play("blip");
      resumable = true;
      mainMenuIndex = 0;
      setState("menu");
    },
  },
];

let mainMenuIndex = 0;
let pauseMenuIndex = 0;

const mainMenuItems = () => (resumable ? [CONTINUE_ITEM, ...MAIN_MENU] : MAIN_MENU);

function menuNav(code, items, index) {
  const up = code === "ArrowUp" || code === "KeyW";
  const down = code === "ArrowDown" || code === "KeyS";
  if (!up && !down) return index;
  audio.play("blip");
  return (index + (up ? -1 : 1) + items.length) % items.length;
}

function handleStateKey(code) {
  const enter = code === "Enter" || code === "NumpadEnter";
  const esc = code === "Escape";

  switch (state) {
    case "intro":
      if (enter) {
        audio.play("blip");
        mainMenuIndex = 0;
        setState("menu");
      } else if (/^Digit[1-9]$/.test(code)) {
        // debug: a number key on the title screen warps to that level
        const n = Number(code[5]);
        if (n <= LEVEL_COUNT) {
          audio.play("start");
          newGame(n - 1);
        }
      }
      break;
    case "menu":
      mainMenuIndex = menuNav(code, mainMenuItems(), mainMenuIndex);
      if (enter) {
        mainMenuItems()[mainMenuIndex].action();
      } else if (esc) {
        audio.play("blip");
        setState("intro");
      }
      break;
    case "controls":
    case "quit":
      if (enter || esc) {
        audio.play("blip");
        setState("menu");
      }
      break;
    case "levelstart":
      if (enter) beginLevel();
      break;
    case "playing":
      if (esc) {
        setState("paused");
        pauseMenuIndex = 0;
        document.exitPointerLock?.();
        audio.play("blip");
      }
      break;
    case "paused":
      pauseMenuIndex = menuNav(code, PAUSE_MENU, pauseMenuIndex);
      if (esc) {
        audio.play("blip");
        setState("playing");
      } else if (enter) {
        PAUSE_MENU[pauseMenuIndex].action();
      } else if (code === "KeyQ") {
        resumable = true;
        mainMenuIndex = 0;
        setState("menu");
      }
      break;
    case "died":
      if (enter) restartLevel();
      break;
    case "levelcomplete":
      if (enter) nextLevel();
      break;
    case "gameover":
      if (enter) {
        audio.play("start");
        newGame();
      } else if (esc) {
        setState("intro");
      }
      break;
    case "victory":
      if (enter || esc) setState("intro");
      break;
  }
}

// ------------------------------------------------------------------ flow

function setState(s) {
  state = s;
  stateTimer = 0;
  // once the run has ended there is nothing left to continue
  if (s === "gameover" || s === "victory") resumable = false;
}

function newGame(startIndex = 0) {
  resumable = false;
  session = {
    score: 0,
    lives: 3,
    nextLife: 40000,
    totalKills: 0,
    totalTreasure: 0,
    totalEnemies: 0,
    maxTreasure: 0,
    totalSecretsFound: 0,
    maxSecrets: 0,
    totalTime: 0,
  };
  startLevel(startIndex, null);
}

function startLevel(index, persist) {
  game = new Game(levels[index], index, session, persist);
  setState("levelstart");
}

function beginLevel() {
  setState("playing");
  audio.play("blip");
}

function restartLevel() {
  startLevel(game.levelIndex, null); // classic rules: fresh crossbow after death
}

function nextLevel() {
  const p = game.player;
  session.totalEnemies += game.totalEnemies;
  session.maxTreasure += game.totalTreasure;
  session.maxSecrets += game.totalSecrets;
  session.totalTime += game.time;
  if (game.levelIndex + 1 >= LEVEL_COUNT) {
    audio.play("victory");
    setState("victory");
  } else {
    startLevel(game.levelIndex + 1, {
      health: p.health,
      ammo: p.ammo,
      weapon: p.weapon,
      hasArbalest: p.hasArbalest,
    });
  }
}

function levelBonus() {
  const killPct = game.totalEnemies ? game.kills / game.totalEnemies : 1;
  const treasurePct = game.totalTreasure ? game.treasure / game.totalTreasure : 1;
  const timeBonus = Math.max(0, Math.floor(120 - game.time)) * 10;
  return Math.round(killPct * 1000 + treasurePct * 1000 + timeBonus);
}

// ------------------------------------------------------------------ loop

let last = performance.now();

function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000);
  last = now;
  clock += dt;
  stateTimer += dt;

  update(dt);
  render();
  requestAnimationFrame(frame);
}

function update(dt) {
  if (state !== "playing") return;
  game.update(dt, input);

  if (game.playerDead) {
    deathDelay += dt;
    if (deathDelay > 1.4) {
      deathDelay = 0;
      session.lives--;
      document.exitPointerLock?.();
      if (session.lives <= 0) {
        audio.play("gameOver");
        setState("gameover");
      } else {
        setState("died");
      }
    }
  } else {
    deathDelay = 0;
  }

  if (game.completed && state === "playing") {
    game.bonus = levelBonus();
    game.addScore(game.bonus);
    document.exitPointerLock?.();
    setState("levelcomplete");
  }
}

// ------------------------------------------------------------------ draw

function render() {
  ctx.imageSmoothingEnabled = false;
  switch (state) {
    case "loading":
      drawCenterScreen("#101018", ["LOADING..."]);
      break;
    case "error":
      drawError();
      break;
    case "intro":
      drawIntro();
      break;
    case "menu":
      drawMainMenu();
      break;
    case "controls":
      drawControls();
      break;
    case "quit":
      drawQuit();
      break;
    case "levelstart":
      drawLevelStart();
      break;
    case "playing":
    case "paused":
    case "died":
      renderer.renderView(game);
      if (game.showMap && state === "playing") renderer.renderMap(game);
      renderer.renderHUD(game);
      if (state === "paused") drawPauseOverlay();
      if (state === "died") drawDiedOverlay();
      if (game.playerDead && state === "playing") drawDeathFade();
      break;
    case "levelcomplete":
      drawLevelComplete();
      break;
    case "gameover":
      drawGameOver();
      break;
    case "victory":
      drawVictory();
      break;
  }
}

const blink = () => Math.floor(clock * 2) % 2 === 0;

function text(str, x, y, size, color, align = "center") {
  ctx.font = `bold ${size}px Georgia, serif`;
  ctx.textAlign = align;
  ctx.fillStyle = color;
  ctx.fillText(str, x, y);
}

function titleText(str, x, y, size, color) {
  ctx.font = `bold ${size}px Georgia, serif`;
  ctx.textAlign = "center";
  ctx.fillStyle = "#000";
  ctx.fillText(str, x + size * 0.06, y + size * 0.06);
  ctx.fillStyle = color;
  ctx.fillText(str, x, y);
}

function drawCenterScreen(bg, lines) {
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, W, H);
  lines.forEach((l, i) => text(l, W / 2, H / 2 + i * 24, 16, "#e8e8e8"));
}

function drawError() {
  ctx.fillStyle = "#180808";
  ctx.fillRect(0, 0, W, H);
  text("FAILED TO LOAD LEVELS", W / 2, 150, 22, "#e05050");
  text(errorMsg, W / 2, 190, 12, "#c8c8c8");
  text("Run a local server, e.g.:", W / 2, 240, 14, "#e8e8e8");
  text("python3 -m http.server 8000", W / 2, 268, 14, "#ffd23c");
  text("then open http://localhost:8000", W / 2, 296, 14, "#e8e8e8");
}

function drawMenu(items, index, cy, lineH, size) {
  items.forEach((it, i) => {
    const y = cy + i * lineH;
    const sel = i === index;
    const label = it.label();
    if (sel) {
      titleText(label, W / 2, y, size, blink() ? "#e9c93c" : "#ead9b0");
      // a bolt-tip pointer beside the chosen line, Wolf3D-style
      ctx.font = `bold ${size}px Georgia, serif`;
      const half = ctx.measureText(label).width / 2;
      const py = y - size * 0.34;
      ctx.fillStyle = blink() ? "#e9c93c" : "#c9a24a";
      ctx.beginPath();
      ctx.moveTo(W / 2 - half - 26, py - size * 0.3);
      ctx.lineTo(W / 2 - half - 26, py + size * 0.3);
      ctx.lineTo(W / 2 - half - 12, py);
      ctx.closePath();
      ctx.fill();
    } else {
      text(label, W / 2, y, size, "#a89878");
    }
  });
}

// Fit a menu into the vertical span [top, bottom]: full size while it fits,
// proportionally smaller line height and font once items no longer do.
function drawMenuFitted(items, index, top, bottom, maxLineH, maxSize) {
  const lineH = Math.min(maxLineH, Math.floor((bottom - top) / (items.length + 1)));
  const size = Math.round((maxSize * lineH) / maxLineH);
  // centre the block in the span; baselines sit under the glyphs, so
  // offset by the cap height
  const capH = size * 0.72;
  const cy = Math.round((top + bottom) / 2 + capH / 2 - ((items.length - 1) * lineH) / 2);
  drawMenu(items, index, cy, lineH, size);
}

function drawIntro() {
  ctx.fillStyle = "#140e08";
  ctx.fillRect(0, 0, W, H);
  // torch glow behind the title
  const glow = ctx.createRadialGradient(W / 2, 105, 10, W / 2, 105, 280);
  glow.addColorStop(0, "rgba(200,120,40,0.22)");
  glow.addColorStop(1, "rgba(200,120,40,0)");
  ctx.fillStyle = glow;
  ctx.fillRect(0, 0, W, H);

  titleText("F O R T R E S S", W / 2, 110, 52, "#e9c93c");
  ctx.fillStyle = "#c9a24a";
  ctx.fillRect(W / 2 - 150, 130, 300, 2);
  text("A MEDIEVAL RAYCASTING ADVENTURE", W / 2, 158, 11, "#a89878");

  const story = [
    "Betrayed and thrown into the fortress dungeons,",
    "you have slipped your chains. Five floors of the",
    "garrison stand between you and the night. Find the",
    "3 KEYS on each floor, unlock the GATE, and escape.",
  ];
  story.forEach((l, i) => text(l, W / 2, 205 + i * 20, 12, "#d8cbaa"));

  if (blink()) titleText("PRESS ENTER", W / 2, 335, 18, "#ead9b0");
}

function drawMainMenu() {
  ctx.fillStyle = "#140e08";
  ctx.fillRect(0, 0, W, H);
  // torch glow behind the heading
  const glow = ctx.createRadialGradient(W / 2, 80, 10, W / 2, 80, 240);
  glow.addColorStop(0, "rgba(200,120,40,0.22)");
  glow.addColorStop(1, "rgba(200,120,40,0)");
  ctx.fillStyle = glow;
  ctx.fillRect(0, 0, W, H);

  titleText("F O R T R E S S", W / 2, 80, 34, "#e9c93c");
  ctx.fillStyle = "#c9a24a";
  ctx.fillRect(W / 2 - 120, 98, 240, 2);

  // the block lives between the heading rule (~y100) and the hint line (~y360)
  drawMenuFitted(mainMenuItems(), mainMenuIndex, 100, 360, 42, 24);

  text("ARROWS — SELECT      ENTER — CONFIRM      ESC — BACK", W / 2, 370, 10, "#786850");
}

function drawControls() {
  ctx.fillStyle = "#140e08";
  ctx.fillRect(0, 0, W, H);
  titleText("CONTROLS", W / 2, 90, 36, "#e9c93c");
  ctx.fillStyle = "#c9a24a";
  ctx.fillRect(W / 2 - 120, 108, 240, 2);

  const controls = [
    ["W A S D / ARROWS", "move & turn"],
    ["MOUSE (CLICK VIEW)", "turn & shoot"],
    ["CTRL / J", "shoot"],
    ["E / SPACE", "open doors, pull levers, use the gate"],
    ["1 / 2 / 3", "dagger, crossbow, repeater"],
    ["SHIFT / TAB / M", "run / map / sound"],
    ["F", "fullscreen"],
    ["ESC", "pause menu"],
  ];
  controls.forEach(([k, v], i) => {
    const y = 150 + i * 24;
    text(k, W / 2 - 12, y, 13, "#c9a24a", "right");
    text(v, W / 2 + 2, y, 13, "#d8cbaa", "left");
  });

  if (blink()) text("ESC / ENTER — BACK", W / 2, 350, 13, "#ead9b0");
}

function drawQuit() {
  ctx.fillStyle = "#140e08";
  ctx.fillRect(0, 0, W, H);
  titleText("FLEEING ALREADY?", W / 2, 140, 32, "#e9c93c");
  text("The fortress will be waiting for your return.", W / 2, 185, 14, "#d8cbaa");
  text("You may close this tab now...", W / 2, 215, 14, "#d8cbaa");
  if (blink()) text("...OR PRESS ENTER TO STAY AND FIGHT", W / 2, 270, 14, "#ead9b0");
}

function drawLevelStart() {
  ctx.fillStyle = "#140e08";
  ctx.fillRect(0, 0, W, H);
  text(`FLOOR ${game.levelIndex + 1} OF ${LEVEL_COUNT}`, W / 2, 130, 16, "#a89878");
  titleText(game.level.name, W / 2, 180, 36, "#e9c93c");
  text("FIND 3 KEYS AND REACH THE GATE", W / 2, 230, 14, "#d8cbaa");
  text(`LIVES: ${session.lives}    SCORE: ${session.score}`, W / 2, 262, 13, "#a89878");
  if (blink()) text("PRESS ENTER", W / 2, 320, 16, "#ead9b0");
  if (stateTimer > 3) beginLevel();
}

function drawPauseOverlay() {
  ctx.fillStyle = "rgba(0,0,0,0.6)";
  ctx.fillRect(0, 0, W, VIEW_H);
  titleText("PAUSED", W / 2, 110, 34, "#e9c93c");
  drawMenuFitted(PAUSE_MENU, pauseMenuIndex, 125, VIEW_H - 10, 28, 17);
}

function drawDeathFade() {
  ctx.fillStyle = `rgba(120,0,0,${Math.min(0.7, deathDelay * 0.6)})`;
  ctx.fillRect(0, 0, W, VIEW_H);
}

function drawDiedOverlay() {
  ctx.fillStyle = "rgba(60,0,0,0.7)";
  ctx.fillRect(0, 0, W, VIEW_H);
  titleText("YOU HAVE FALLEN", W / 2, 130, 36, "#e05050");
  text(`LIVES LEFT: ${session.lives}`, W / 2, 175, 16, "#ead9b0");
  if (blink()) text("PRESS ENTER TO TRY THIS FLOOR AGAIN", W / 2, 220, 14, "#e9c93c");
}

function fmtTime(t) {
  const m = Math.floor(t / 60);
  const s = Math.floor(t % 60);
  return `${m}:${String(s).padStart(2, "0")}`;
}

function drawLevelComplete() {
  ctx.fillStyle = "#140e08";
  ctx.fillRect(0, 0, W, H);
  text(`FLOOR ${game.levelIndex + 1} CLEARED!`, W / 2, 90, 26, "#e9c93c");

  const killPct = game.totalEnemies ? Math.round((100 * game.kills) / game.totalEnemies) : 100;
  const trePct = game.totalTreasure ? Math.round((100 * game.treasure) / game.totalTreasure) : 100;
  const rows = [
    ["TIME", fmtTime(game.time)],
    ["KILLS", `${game.kills}/${game.totalEnemies}  (${killPct}%)`],
    ["TREASURE", `${game.treasure}/${game.totalTreasure}  (${trePct}%)`],
    ["SECRETS", `${game.secretsFound}/${game.totalSecrets}`],
    ["BONUS", `+${game.bonus}`],
    ["SCORE", `${session.score}`],
  ];
  rows.forEach(([k, v], i) => {
    const y = 145 + i * 28;
    text(k, W / 2 - 30, y, 15, "#a89878", "right");
    text(v, W / 2 - 5, y, 15, "#ead9b0", "left");
  });

  const lastOne = game.levelIndex + 1 >= LEVEL_COUNT;
  if (blink()) {
    text(lastOne ? "PRESS ENTER" : "PRESS ENTER FOR THE NEXT FLOOR", W / 2, 330, 15, "#e9c93c");
  }
}

function drawGameOver() {
  ctx.fillStyle = "#180808";
  ctx.fillRect(0, 0, W, H);
  titleText("GAME OVER", W / 2, 150, 52, "#c02020");
  text(`FINAL SCORE: ${session.score}`, W / 2, 205, 18, "#ead9b0");
  text(`KILLS: ${session.totalKills}`, W / 2, 235, 13, "#a89878");
  if (blink()) text("ENTER — PLAY AGAIN", W / 2, 290, 15, "#e9c93c");
  text("ESC — TITLE SCREEN", W / 2, 318, 13, "#d8cbaa");
}

function drawVictory() {
  ctx.fillStyle = "#100c14";
  ctx.fillRect(0, 0, W, H);
  titleText("YOU ESCAPED THE FORTRESS!", W / 2, 90, 30, "#e9c93c");
  text("WELL FOUGHT — YOU ARE FREE.", W / 2, 128, 14, "#d8cbaa");
  text("Thanks for playing this little retro adventure.", W / 2, 150, 12, "#a89878");

  const killPct = session.totalEnemies
    ? Math.round((100 * session.totalKills) / session.totalEnemies)
    : 100;
  const trePct = session.maxTreasure
    ? Math.round((100 * session.totalTreasure) / session.maxTreasure)
    : 100;
  const rows = [
    ["TOTAL TIME", fmtTime(session.totalTime)],
    ["KILLS", `${session.totalKills}/${session.totalEnemies}  (${killPct}%)`],
    ["TREASURE", `${session.totalTreasure}/${session.maxTreasure}  (${trePct}%)`],
    ["SECRETS", `${session.totalSecretsFound}/${session.maxSecrets}`],
    ["LIVES LEFT", `${session.lives}`],
    ["FINAL SCORE", `${session.score}`],
  ];
  rows.forEach(([k, v], i) => {
    const y = 190 + i * 25;
    text(k, W / 2 - 30, y, 14, "#a89878", "right");
    text(v, W / 2 - 5, y, 14, "#ead9b0", "left");
  });

  if (blink()) text("PRESS ENTER FOR THE TITLE SCREEN", W / 2, 350, 14, "#e9c93c");
}

// debug/testing hook (harmless in normal play)
window.FORTRESS = {
  getGame: () => game,
  getState: () => state,
  // warp(3) in the console starts a fresh run on level 3
  warp: (n) => {
    if (!levels) return "levels not loaded yet";
    const idx = Math.floor(n) - 1;
    if (idx < 0 || idx >= LEVEL_COUNT) return `level must be 1-${LEVEL_COUNT}`;
    newGame(idx);
    return `warped to level ${n} — press ENTER`;
  },
};

// ------------------------------------------------------------------ boot

loadLevels(LEVEL_COUNT)
  .then((lv) => {
    levels = lv;
    // debug: ?level=N skips the title screen and starts on that level
    const warp = Number(new URLSearchParams(location.search).get("level"));
    if (Number.isInteger(warp) && warp >= 1 && warp <= LEVEL_COUNT) {
      newGame(warp - 1);
    } else {
      setState("intro");
    }
  })
  .catch((err) => {
    errorMsg = String(err.message ?? err);
    setState("error");
  });

requestAnimationFrame(frame);
