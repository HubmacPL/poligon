// Generates a tree with the EZ-Tree library (MIT) and writes it as glTF with bark and leaf textures.
//
// Usage:
//   node tools/assets/make_tree.mjs NODE_MODULES OUTPUT_DIR NAME PRESET HEIGHT_METERS
//
// NODE_MODULES   folder holding the installed packages @dgreenheck/ez-tree and three
//                (they are installed outside the repository, in poligon-assets/tools/eztree)
// OUTPUT_DIR     folder to write NAME.gltf, NAME.bin and textures/ into
// PRESET         a built-in EZ-Tree preset name such as "Pine Large", or a path to a preset .json
// HEIGHT_METERS  the tree is scaled so that it is this tall
//
// EZ-Tree builds the trunk and branches as tubes and the foliage as flat squares showing a
// photo of a twig. Only its geometry is used here; the glTF is written by this script, because
// EZ-Tree's own export needs a web browser.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulesDir, outputDir, name, preset, heightText] = process.argv.slice(2);
if (!heightText) {
  console.error('usage: node make_tree.mjs NODE_MODULES OUTPUT_DIR NAME PRESET HEIGHT_METERS');
  process.exit(2);
}
const targetHeight = Number(heightText);

// EZ-Tree starts loading its textures as soon as it is imported, through browser image
// elements. Outside a browser there are none; this stand-in lets the import succeed.
// The textures themselves are copied as files further down.
globalThis.document = {
  createElementNS: () => ({ addEventListener() {}, removeEventListener() {}, style: {} }),
};

const packageDir = path.join(modulesDir, '@dgreenheck', 'ez-tree');
const { Tree } = await import(pathToFileURL(path.join(packageDir, 'build', 'ez-tree.es.js')).href);

const tree = new Tree();
if (preset.toLowerCase().endsWith('.json')) {
  tree.loadFromJson(JSON.parse(fs.readFileSync(preset, 'utf8')));
} else {
  tree.loadPreset(preset);
}
tree.generate();

const parts = [
  { label: 'bark', geometry: tree.branchesMesh.geometry },
  { label: 'leaves', geometry: tree.leavesMesh.geometry },
];

// Height of the unscaled tree, to compute the scale.
let top = 0;
for (const part of parts) {
  const positions = part.geometry.getAttribute('position');
  for (let i = 0; i < positions.count; i++) top = Math.max(top, positions.getY(i));
}
const scale = targetHeight / top;

// One binary buffer holding, for each part: positions, normals, texture coordinates, indices.
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
let triangleTotal = 0;
parts.forEach((part, partIndex) => {
  const source = part.geometry;
  const sourcePositions = source.getAttribute('position');
  const sourceNormals = source.getAttribute('normal');
  const sourceUvs = source.getAttribute('uv');
  const count = sourcePositions.count;

  const positions = new Float32Array(count * 3);
  const normals = new Float32Array(count * 3);
  const uvs = new Float32Array(count * 2);
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < count; i++) {
    const p = [sourcePositions.getX(i) * scale, sourcePositions.getY(i) * scale, sourcePositions.getZ(i) * scale];
    positions.set(p, i * 3);
    for (let axis = 0; axis < 3; axis++) {
      min[axis] = Math.min(min[axis], p[axis]);
      max[axis] = Math.max(max[axis], p[axis]);
    }
    normals.set([sourceNormals.getX(i), sourceNormals.getY(i), sourceNormals.getZ(i)], i * 3);
    // three.js puts texture coordinate (0, 0) at the bottom-left, glTF at the top-left.
    uvs.set([sourceUvs.getX(i), 1 - sourceUvs.getY(i)], i * 2);
  }
  const indices = Uint32Array.from(source.getIndex().array);
  triangleTotal += indices.length / 3;

  const ARRAY_BUFFER = 34962;
  const ELEMENT_ARRAY_BUFFER = 34963;
  const FLOAT = 5126;
  const UNSIGNED_INT = 5125;
  const base = accessors.length;
  accessors.push(
    { bufferView: addView(positions, ARRAY_BUFFER), componentType: FLOAT, count, type: 'VEC3', min, max },
    { bufferView: addView(normals, ARRAY_BUFFER), componentType: FLOAT, count, type: 'VEC3' },
    { bufferView: addView(uvs, ARRAY_BUFFER), componentType: FLOAT, count, type: 'VEC2' },
    { bufferView: addView(indices, ELEMENT_ARRAY_BUFFER), componentType: UNSIGNED_INT, count: indices.length, type: 'SCALAR' },
  );
  meshes.push({
    name: `${name}_${part.label}`,
    primitives: [{
      attributes: { POSITION: base, NORMAL: base + 1, TEXCOORD_0: base + 2 },
      indices: base + 3,
      material: partIndex,
    }],
  });
});

// Textures come from the EZ-Tree package, chosen by the preset's bark and leaf type.
const options = tree.options;
const textureDir = path.join(outputDir, 'textures');
fs.mkdirSync(textureDir, { recursive: true });
const barkFile = `${options.bark.type}_color_1k.jpg`;
const leafFile = `${options.leaves.type}_color.png`;
fs.copyFileSync(path.join(packageDir, 'src', 'lib', 'assets', 'bark', barkFile), path.join(textureDir, barkFile));
fs.copyFileSync(path.join(packageDir, 'src', 'lib', 'assets', 'leaves', leafFile), path.join(textureDir, leafFile));

// A preset tint is a color such as 0x8fa67c that multiplies the texture. glTF wants it as
// linear red, green, blue, alpha, so the sRGB-encoded bytes are converted.
const tintToFactor = (tint) => {
  const toLinear = (byte) => {
    const c = byte / 255;
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  };
  return [toLinear((tint >> 16) & 255), toLinear((tint >> 8) & 255), toLinear(tint & 255), 1];
};

const gltf = {
  asset: { version: '2.0', generator: 'Poligon make_tree.mjs (geometry by EZ-Tree)' },
  scene: 0,
  scenes: [{ nodes: [0, 1] }],
  nodes: [{ name: 'bark', mesh: 0 }, { name: 'leaves', mesh: 1 }],
  meshes,
  materials: [
    {
      name: 'bark',
      pbrMetallicRoughness: {
        baseColorTexture: { index: 0 }, baseColorFactor: tintToFactor(options.bark.tint), metallicFactor: 0, roughnessFactor: 1,
      },
    },
    {
      name: 'leaves',
      pbrMetallicRoughness: {
        baseColorTexture: { index: 1 }, baseColorFactor: tintToFactor(options.leaves.tint), metallicFactor: 0, roughnessFactor: 0.8,
      },
      alphaMode: 'MASK',
      alphaCutoff: options.leaves.alphaTest ?? 0.5,
      doubleSided: true,
    },
  ],
  textures: [{ source: 0, sampler: 0 }, { source: 1, sampler: 0 }],
  images: [{ uri: `textures/${barkFile}` }, { uri: `textures/${leafFile}` }],
  samplers: [{ wrapS: 10497, wrapT: 10497 }],
  accessors,
  bufferViews,
  buffers: [{ uri: `${name}.bin`, byteLength }],
};

fs.writeFileSync(path.join(outputDir, `${name}.bin`), Buffer.concat(chunks));
fs.writeFileSync(path.join(outputDir, `${name}.gltf`), JSON.stringify(gltf, null, 1));
console.log(`make_tree: ${name}: ${triangleTotal} triangles, ${targetHeight} m tall, wrote ${path.join(outputDir, name + '.gltf')}`);
