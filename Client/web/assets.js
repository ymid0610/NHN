// Art from the Unity project, used directly rather than copied.
//
// The page is served from Client/, so these resolve to the same files the Unity
// client uses — nothing is duplicated and nothing here can drift from the real
// assets. That is also why the paths are relative to Client/ and not to web/.
//
// Only the pieces that map cleanly onto something the server actually sends are
// listed. Items with no art (tumbleweed, balloon, paint can, grenade) are drawn
// procedurally in a matching palette; a wrong sprite would be worse than an
// honest shape.

import { ItemKind } from './protocol.js';

const BASE = '../Assets/';

const MANIFEST = {
  background: 'Resources/UI/WesternGameplayBackgroundGenerated.png',
  lobbyBackground: 'Resources/UI/WesternLobbyBackgroundGenerated.png',
  crosshair: 'Sprite/CrossHairRed.png',
  bulletHole: 'Sprite/BulletHole.png',

  // Items the art covers.
  pan: 'Sprite/Generated/FryingPanGenerated.png',
  poster: 'Sprite/Generated/OutlawBanditGenerated.png',
  speedloader: 'Sprite/Generated/TripleShotPowerupGenerated.png',
};

/// The board face comes from the first cell of the GomokuPaper sheet: a flat,
/// front-facing square of ruled paper.
///
/// The project also has a lovely eight-frame animation of the sheet fluttering
/// as it falls, and it is deliberately not used. Those frames are curled, so a
/// grid drawn over them would sit on a curved surface while the server's hit box
/// stays a rectangle — near a cell edge that is the difference between a hit and
/// a miss. Aiming fidelity is the one thing this client exists to prove, so the
/// flat face wins over the prettier one.
const PAPER_SHEET = 'Sprite/GomokuPaper.png';
const PAPER_COLUMNS = 6;
const PAPER_ROWS = 8;

export const Art = {
  images: {},
  /// { image, sx, sy, sw, sh } for the board face, or null before load.
  boardFace: null,
  ready: false,
  loaded: 0,
  total: 0,

  /// Sprite for an item kind, or null when it should be drawn procedurally.
  itemSprite(kind) {
    return this.images[ItemKind[kind]] ?? null;
  },
};

/// Tight bounding box of the opaque pixels in a region of an image.
///
/// Measured rather than hardcoded: the art is generated, and a regenerated sheet
/// with different padding would silently misalign every board if these were
/// magic numbers.
function opaqueBounds(image, sx, sy, sw, sh) {
  const probe = document.createElement('canvas');
  probe.width = sw;
  probe.height = sh;
  const g = probe.getContext('2d', { willReadFrequently: true });
  g.drawImage(image, sx, sy, sw, sh, 0, 0, sw, sh);

  const { data } = g.getImageData(0, 0, sw, sh);
  let minX = sw, minY = sh, maxX = -1, maxY = -1;
  for (let y = 0; y < sh; y++) {
    for (let x = 0; x < sw; x++) {
      if (data[(y * sw + x) * 4 + 3] < 24) continue;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
  }
  if (maxX < 0) return { sx, sy, sw, sh };
  return { sx: sx + minX, sy: sy + minY, sw: maxX - minX + 1, sh: maxY - minY + 1 };
}

function load(src) {
  return new Promise((resolve) => {
    const image = new Image();
    // Resolving on error too: a missing asset degrades that one sprite to its
    // procedural fallback instead of stalling the whole client.
    image.onload = () => { Art.loaded++; resolve(image); };
    image.onerror = () => { Art.loaded++; console.warn(`missing asset: ${src}`); resolve(null); };
    image.src = src;
  });
}

export async function loadArt(onProgress) {
  const entries = Object.entries(MANIFEST);
  Art.total = entries.length + 1;

  const tick = () => onProgress?.(Art.loaded, Art.total);
  const timer = setInterval(tick, 100);

  const [named, sheet] = await Promise.all([
    Promise.all(entries.map(async ([key, path]) => [key, await load(BASE + path)])),
    load(BASE + PAPER_SHEET),
  ]);

  clearInterval(timer);

  for (const [key, image] of named) {
    if (image) Art.images[key] = image;
  }

  if (sheet) {
    const cellW = sheet.naturalWidth / PAPER_COLUMNS;
    const cellH = sheet.naturalHeight / PAPER_ROWS;
    Art.boardFace = { image: sheet, ...opaqueBounds(sheet, 0, 0, cellW, cellH) };
  }

  Art.ready = true;
  tick();
  return Art;
}
