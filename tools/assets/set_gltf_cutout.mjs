// Makes every material of a glTF file that uses a given base color image a cut-out material
// and points it at a replacement image that has transparency.
//
// Usage: node tools/assets/set_gltf_cutout.mjs MODEL.gltf OLD_IMAGE_URI NEW_IMAGE_URI
//
// Example: node set_gltf_cutout.mjs fern.gltf textures/fern_diff_2k.jpg textures/fern_diff_alpha_2k.png
// The first time, the untouched file is kept next to the model as MODEL.gltf.original.
import fs from 'node:fs';

const [modelPath, oldUri, newUri] = process.argv.slice(2);
if (!newUri) {
  console.error('usage: node set_gltf_cutout.mjs MODEL.gltf OLD_IMAGE_URI NEW_IMAGE_URI');
  process.exit(2);
}

const text = fs.readFileSync(modelPath, 'utf8');
const gltf = JSON.parse(text);

const imageIndex = (gltf.images ?? []).findIndex((image) => image.uri === oldUri || image.uri === newUri);
if (imageIndex < 0) {
  console.error(`set_gltf_cutout: ${modelPath} has no image "${oldUri}"`);
  process.exit(1);
}
gltf.images[imageIndex].uri = newUri;
delete gltf.images[imageIndex].mimeType;

// Materials refer to textures, textures to images.
let changed = 0;
for (const material of gltf.materials ?? []) {
  const textureIndex = material.pbrMetallicRoughness?.baseColorTexture?.index;
  if (textureIndex !== undefined && gltf.textures[textureIndex].source === imageIndex) {
    material.alphaMode = 'MASK';
    material.alphaCutoff = 0.5;
    material.doubleSided = true;
    changed++;
  }
}

if (!fs.existsSync(modelPath + '.original')) {
  fs.writeFileSync(modelPath + '.original', text);
}
fs.writeFileSync(modelPath, JSON.stringify(gltf, null, 1));
console.log(`set_gltf_cutout: ${modelPath}: ${changed} material(s) now cut out with ${newUri}`);
