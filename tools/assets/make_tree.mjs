// Generates a tree with the EZ-Tree library (MIT) and writes it as glTF, with levels of detail.
//
// Usage:
//   node tools/assets/make_tree.mjs NODE_MODULES ASSETS_FOLDER RECIPE.json
//
// NODE_MODULES   folder holding the installed packages @dgreenheck/ez-tree and three
//                (they are installed outside the repository, in poligon-assets/tools/eztree)
// ASSETS_FOLDER  the asset sources, poligon-assets/source; texture paths in the recipe are relative to it
// RECIPE.json    what to make; ours are in tools/assets/trees/ (format below)
//
// Writes ASSETS_FOLDER/generated/NAME/NAME.gltf (full detail, with its textures) and
// ASSETS_FOLDER/lods/NAME/lodN/NAME.gltf for the simpler levels, the layout forest_scene reads.
//
// EZ-Tree builds the trunk and branches as tubes and the foliage as flat squares ("cards")
// showing a picture of a twig. Only its geometry is used; the glTF is written here, because
// EZ-Tree's own export needs a web browser. On top of EZ-Tree this script:
//   - maps bark textures by real size (meters per repeat) instead of once per tube section,
//   - shows a different twig from a texture atlas on each card, at the twig's proportions,
//     every other one mirrored; twigs in the atlas must grow upwards (base at the bottom),
//   - can bend card normals outwards, so a crown is lit as a round mass rather than a pile of planes,
//   - makes simpler levels from the same tree: fewer sides on each tube (this keeps the
//     random shape identical, unlike fewer sections), thin branches left out, fewer but
//     larger cards. The script checks that every level has exactly the same cards.
//
// Recipe:
// {
//   "name": "spruce",
//   "base": "pine_large",                 EZ-Tree preset file name (src/lib/presets), optional
//   "options": { ... },                   EZ-Tree options, merged over the base preset
//   "height": 20,                         the tree is scaled to this height in meters
//   "bark": { "color": "...", "normal": "...", "orm": "...", "meters": 1.2 },
//   "leaves": { "color": "...", "normal": "...", "orm": "...",
//               "atlas": [[x0, y0, x1, y1], ...],   twig rectangles, 0..1 from the top left
//               "alpha_cutoff": 0.4, "roundness": 0.5, "tint": [r, g, b] },
//   "levels": [                           level 0 first; each entry may give:
//     { "segments": {"0": 8, "1": 5},     sides of the tubes per branch level
//       "drop_branch_level": 2,           leave out tubes of this branch level and deeper
//       "min_radius": 0.02,               leave out tubes thinner than this (meters)
//       "leaf_keep": 0.5,                 keep this part of the cards, enlarged to cover the same area
//       "single_cards": true }            drop the second, crossed square of each card
//   ]
// }
// "normal" and "orm" (glTF layout: occlusion, roughness, metallic) are optional.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulesDir, assetsDir, recipePath] = process.argv.slice(2);
if (!recipePath) {
  console.error('usage: node make_tree.mjs NODE_MODULES ASSETS_FOLDER RECIPE.json');
  process.exit(2);
}
const recipe = JSON.parse(fs.readFileSync(recipePath, 'utf8'));
const name = recipe.name;

// EZ-Tree starts loading its textures as soon as it is imported, through browser image
// elements. Outside a browser there are none; this stand-in lets the import succeed.
globalThis.document = {
  createElementNS: () => ({ addEventListener() {}, removeEventListener() {}, style: {} }),
};

const packageDir = path.join(modulesDir, '@dgreenheck', 'ez-tree');
const { Tree } = await import(pathToFileURL(path.join(packageDir, 'build', 'ez-tree.es.js')).href);

const deepMerge = (target, source) => {
  for (const [key, value] of Object.entries(source ?? {})) {
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      target[key] = deepMerge(target[key] && typeof target[key] === 'object' ? target[key] : {}, value);
    } else {
      target[key] = value;
    }
  }
  return target;
};

const baseOptions = recipe.base
  ? JSON.parse(fs.readFileSync(path.join(packageDir, 'src', 'lib', 'presets', `${recipe.base}.json`), 'utf8'))
  : {};
const treeOptions = deepMerge(baseOptions, recipe.options);
const doubleCards = (treeOptions.leaves?.billboard ?? 'double') === 'double';

// A small random number generator of our own (mulberry32), so that the choice of twigs and
// of the cards kept does not depend on EZ-Tree's.
const makeRandom = (seed) => {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = state;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
};

// Generates the tree with the given tube sides and records where each branch's vertices,
// triangles and cards are.
const generate = (segments) => {
  const options = deepMerge(JSON.parse(JSON.stringify(treeOptions)), { branch: { segments: segments ?? {} } });
  const tree = new Tree();
  const branches = [];
  const original = tree.generateBranch.bind(tree);
  tree.generateBranch = (branch) => {
    const record = {
      level: branch.level,
      radius: branch.radius,
      sectionCount: branch.sectionCount,
      segmentCount: branch.segmentCount,
      vertexStart: tree.branches.verts.length / 3,
      indexStart: tree.branches.indices.length,
    };
    original(branch);
    record.vertexEnd = tree.branches.verts.length / 3;
    record.indexEnd = tree.branches.indices.length;
    branches.push(record);
  };
  tree.loadFromJson(options);
  return { tree, branches };
};

const full = generate(recipe.levels?.[0]?.segments);

// Height of the unscaled tree, to compute the scale.
let top = 0;
for (const array of [full.tree.branches.verts, full.tree.leaves.verts]) {
  for (let i = 1; i < array.length; i += 3) top = Math.max(top, array[i]);
}
const scale = recipe.height / top;

const ARRAY_BUFFER = 34962;
const ELEMENT_ARRAY_BUFFER = 34963;
const FLOAT = 5126;
const UNSIGNED_INT = 5125;

// Bark: tubes keep EZ-Tree's shape; texture coordinates are recomputed so that the texture
// repeats every `meters` along the branch and a whole number of times around it.
const buildBark = ({ tree, branches }, level) => {
  const source = tree.branches;
  const barkMeters = recipe.bark.meters ?? 1.0;
  const positions = [];
  const normals = [];
  const uvs = [];
  const indices = [];
  for (const branch of branches) {
    if (level.drop_branch_level !== undefined && branch.level >= level.drop_branch_level) continue;
    if (level.min_radius !== undefined && branch.level > 0 && branch.radius * scale < level.min_radius) continue;
    const ringSize = branch.segmentCount + 1;
    const ringCount = branch.sectionCount + 1;
    if ((branch.vertexEnd - branch.vertexStart) !== ringSize * ringCount) {
      throw new Error(`${name}: unexpected branch layout`);
    }
    const base = positions.length / 3;
    const repeats = Math.max(1, Math.round((2 * Math.PI * branch.radius * scale) / barkMeters));
    let along = 0;
    let previousCenter = null;
    for (let ring = 0; ring < ringCount; ring++) {
      const center = [0, 0, 0];
      for (let j = 0; j < branch.segmentCount; j++) {
        const v = (branch.vertexStart + ring * ringSize + j) * 3;
        for (let axis = 0; axis < 3; axis++) center[axis] += (source.verts[v + axis] * scale) / branch.segmentCount;
      }
      if (previousCenter) along += Math.hypot(center[0] - previousCenter[0], center[1] - previousCenter[1], center[2] - previousCenter[2]);
      previousCenter = center;
      for (let j = 0; j < ringSize; j++) {
        const v = (branch.vertexStart + ring * ringSize + j) * 3;
        positions.push(source.verts[v] * scale, source.verts[v + 1] * scale, source.verts[v + 2] * scale);
        normals.push(source.normals[v], source.normals[v + 1], source.normals[v + 2]);
        uvs.push((j / branch.segmentCount) * repeats, along / barkMeters);
      }
    }
    for (let i = branch.indexStart; i < branch.indexEnd; i++) {
      indices.push(source.indices[i] - branch.vertexStart + base);
    }
  }
  return { positions, normals, uvs, indices };
};

// Twig rectangles, chosen at random weighted by their area so that big twigs appear most.
const atlas = recipe.leaves.atlas ?? [[0, 0, 1, 1]];
const atlasWeights = atlas.map(([x0, y0, x1, y1]) => (x1 - x0) * (y1 - y0));
const atlasTotal = atlasWeights.reduce((a, b) => a + b, 0);

const buildLeaves = ({ tree }, level) => {
  const source = tree.leaves;
  const quadsPerCard = doubleCards ? 2 : 1;
  const cardCount = source.verts.length / 3 / 4 / quadsPerCard;
  const keep = level.leaf_keep ?? 1.0;
  const grow = 1 / Math.sqrt(keep);
  const keepRandom = makeRandom((treeOptions.seed ?? 1) * 7 + 1);
  const twigRandom = makeRandom((treeOptions.seed ?? 1) * 13 + 5);
  const roundness = recipe.leaves.roundness ?? 0;
  // Leaves are lit as if the crown were a ball around this point, when roundness > 0.
  let crownCenter = [0, 0, 0];
  for (let i = 0; i < source.verts.length; i += 3) {
    for (let axis = 0; axis < 3; axis++) crownCenter[axis] += (source.verts[i + axis] * scale * 3) / source.verts.length;
  }
  const positions = [];
  const normals = [];
  const uvs = [];
  const indices = [];
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
  const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
  const normalize = (a) => {
    const length = Math.hypot(a[0], a[1], a[2]) || 1;
    return [a[0] / length, a[1] / length, a[2] / length];
  };
  const padding = 0.004;
  for (let card = 0; card < cardCount; card++) {
    // One draw per card in every level, so that the same cards are kept whatever `keep` is.
    const kept = keepRandom() < keep;
    for (let q = 0; q < quadsPerCard; q++) {
      let pick = twigRandom() * atlasTotal;
      let rectIndex = 0;
      while (rectIndex < atlas.length - 1 && pick > atlasWeights[rectIndex]) {
        pick -= atlasWeights[rectIndex];
        rectIndex++;
      }
      if (!kept || (q > 0 && level.single_cards)) continue;
      const first = (card * quadsPerCard + q) * 4;
      const corner = (k) => {
        const v = (first + k) * 3;
        return [source.verts[v] * scale, source.verts[v + 1] * scale, source.verts[v + 2] * scale];
      };
      // EZ-Tree's corners: 0 top left, 1 bottom left, 2 bottom right, 3 top right; the
      // bottom edge sits on the branch.
      const [c0, c1, c2, c3] = [corner(0), corner(1), corner(2), corner(3)];
      const bottom = mul(add(c1, c2), 0.5);
      let up = sub(mul(add(c0, c3), 0.5), bottom);
      let side = sub(c2, c1);
      const [x0, y0, x1, y1] = atlas[rectIndex];
      const aspect = (x1 - x0) / (y1 - y0);
      if (aspect >= 1) up = mul(up, 1 / aspect);
      else side = mul(side, aspect);
      up = mul(up, grow);
      side = mul(side, grow);
      const halfSide = mul(side, 0.5);
      const corners = [add(add(bottom, up), mul(halfSide, -1)), add(bottom, mul(halfSide, -1)), add(bottom, halfSide), add(add(bottom, up), halfSide)];
      const flat = normalize([source.normals[first * 3], source.normals[first * 3 + 1], source.normals[first * 3 + 2]]);
      const base = positions.length / 3;
      // Every other card shows its twig mirrored, which doubles the variety of an atlas.
      const mirrored = (card + q) % 2 === 1;
      const rect = mirrored
        ? [x1 + padding, y0 - padding, x0 - padding, y1 + padding]
        : [x0 - padding, y0 - padding, x1 + padding, y1 + padding];
      const cornerUvs = [[rect[0], rect[1]], [rect[0], rect[3]], [rect[2], rect[3]], [rect[2], rect[1]]];
      for (let k = 0; k < 4; k++) {
        positions.push(...corners[k]);
        const outward = normalize(sub(corners[k], crownCenter));
        // Keep the side the flat normal faces: a double-sided card is lit from both sides.
        const sign = (flat[0] * outward[0] + flat[1] * outward[1] + flat[2] * outward[2]) >= 0 ? 1 : -1;
        normals.push(...normalize(add(mul(flat, 1 - roundness), mul(outward, roundness * sign))));
        uvs.push(...cornerUvs[k]);
      }
      indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }
  }
  return { positions, normals, uvs, indices };
};

// Writes one glTF with a bark mesh and a leaves mesh.
const writeGltf = (folder, parts, imageUri) => {
  fs.mkdirSync(folder, { recursive: true });
  const chunks = [];
  let byteLength = 0;
  const bufferViews = [];
  const accessors = [];
  const addView = (typedArray, target) => {
    const bytes = Buffer.from(typedArray.buffer, typedArray.byteOffset, typedArray.byteLength);
    const padding = (4 - (bytes.length % 4)) % 4;
    bufferViews.push({ buffer: 0, byteOffset: byteLength, byteLength: bytes.length, target });
    chunks.push(bytes, Buffer.alloc(padding));
    byteLength += bytes.length + padding;
    return bufferViews.length - 1;
  };
  const meshes = [];
  const nodes = [];
  let triangles = 0;
  parts.forEach((part, partIndex) => {
    if (part.indices.length === 0) return;
    const positions = Float32Array.from(part.positions);
    const min = [Infinity, Infinity, Infinity];
    const max = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < positions.length; i++) {
      min[i % 3] = Math.min(min[i % 3], positions[i]);
      max[i % 3] = Math.max(max[i % 3], positions[i]);
    }
    const count = positions.length / 3;
    const base = accessors.length;
    accessors.push(
      { bufferView: addView(positions, ARRAY_BUFFER), componentType: FLOAT, count, type: 'VEC3', min, max },
      { bufferView: addView(Float32Array.from(part.normals), ARRAY_BUFFER), componentType: FLOAT, count, type: 'VEC3' },
      { bufferView: addView(Float32Array.from(part.uvs), ARRAY_BUFFER), componentType: FLOAT, count, type: 'VEC2' },
      { bufferView: addView(Uint32Array.from(part.indices), ELEMENT_ARRAY_BUFFER), componentType: UNSIGNED_INT, count: part.indices.length, type: 'SCALAR' },
    );
    triangles += part.indices.length / 3;
    nodes.push({ name: part.label, mesh: meshes.length });
    meshes.push({
      name: `${name}_${part.label}`,
      primitives: [{ attributes: { POSITION: base, NORMAL: base + 1, TEXCOORD_0: base + 2 }, indices: base + 3, material: partIndex }],
    });
  });

  // Materials and their textures.
  const images = [];
  const textures = [];
  const textureFor = (file) => {
    if (!file) return undefined;
    images.push({ uri: imageUri(file) });
    textures.push({ source: images.length - 1, sampler: 0 });
    return { index: textures.length - 1 };
  };
  const tintToFactor = (tint) => {
    const toLinear = (byte) => {
      const c = byte / 255;
      return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
    };
    return [toLinear((tint >> 16) & 255), toLinear((tint >> 8) & 255), toLinear(tint & 255), 1];
  };
  const makeMaterial = (materialName, spec, tint) => {
    const material = {
      name: materialName,
      pbrMetallicRoughness: { baseColorTexture: textureFor(spec.color), baseColorFactor: tint, metallicFactor: 0, roughnessFactor: 1 },
    };
    const normal = textureFor(spec.normal);
    if (normal) material.normalTexture = normal;
    const orm = textureFor(spec.orm);
    if (orm) {
      material.pbrMetallicRoughness.metallicRoughnessTexture = orm;
      material.occlusionTexture = { index: orm.index };
    }
    return material;
  };
  const leafTint = recipe.leaves.tint ? [...recipe.leaves.tint, 1] : tintToFactor(treeOptions.leaves?.tint ?? 0xffffff);
  const barkTint = recipe.bark.tint ? [...recipe.bark.tint, 1] : [1, 1, 1, 1];
  const materials = [
    makeMaterial('bark', recipe.bark, barkTint),
    { ...makeMaterial('leaves', recipe.leaves, leafTint), alphaMode: 'MASK', alphaCutoff: recipe.leaves.alpha_cutoff ?? 0.5, doubleSided: true },
  ];

  const gltf = {
    asset: { version: '2.0', generator: 'Poligon make_tree.mjs (geometry by EZ-Tree)' },
    scene: 0,
    scenes: [{ nodes: nodes.map((_, i) => i) }],
    nodes,
    meshes,
    materials,
    textures,
    images,
    samplers: [{ wrapS: 10497, wrapT: 10497 }],
    accessors,
    bufferViews,
    buffers: [{ uri: `${name}.bin`, byteLength }],
  };
  fs.writeFileSync(path.join(folder, `${name}.bin`), Buffer.concat(chunks));
  fs.writeFileSync(path.join(folder, `${name}.gltf`), JSON.stringify(gltf, null, 1));
  return triangles;
};

// Textures are copied next to the full-detail model; simpler levels point at those copies.
const fullFolder = path.join(assetsDir, 'generated', name);
const textureFolder = path.join(fullFolder, 'textures');
fs.mkdirSync(textureFolder, { recursive: true });
for (const spec of [recipe.bark, recipe.leaves]) {
  for (const key of ['color', 'normal', 'orm']) {
    if (spec[key]) fs.copyFileSync(path.join(assetsDir, spec[key]), path.join(textureFolder, path.basename(spec[key])));
  }
}

const levels = recipe.levels?.length ? recipe.levels : [{}];
const summary = [];
levels.forEach((level, index) => {
  const generated = index === 0 ? full : generate(level.segments);
  if (index > 0) {
    // Fewer tube sides must not change the random shape: the cards must be identical.
    const a = full.tree.leaves.verts;
    const b = generated.tree.leaves.verts;
    if (a.length !== b.length || a.some((value, i) => Math.abs(value - b[i]) > 1e-6)) {
      throw new Error(`${name}: level ${index} came out with different cards; only "segments" may differ between levels`);
    }
  }
  const parts = [
    { label: 'bark', ...buildBark(generated, level) },
    { label: 'leaves', ...buildLeaves(generated, level) },
  ];
  const folder = index === 0 ? fullFolder : path.join(assetsDir, 'lods', name, `lod${index}`);
  const imageUri = (file) =>
    index === 0 ? `textures/${path.basename(file)}` : `../../../generated/${name}/textures/${path.basename(file)}`;
  const triangles = writeGltf(folder, parts, imageUri);
  summary.push(`${triangles} (${parts[0].indices.length / 3} bark + ${parts[1].indices.length / 3} leaves)`);
});
console.log(`make_tree: ${name}: ${recipe.height} m, triangles per level: ${summary.join(', ')}`);
