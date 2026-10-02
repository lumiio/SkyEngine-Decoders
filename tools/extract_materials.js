// 提取 LevelMaterial 材质绑定: material ID → 官方贴图名
const fs = require('fs');
const input = process.argv[2];
const output = process.argv[3];
const d = JSON.parse(fs.readFileSync(input, 'utf8'));
const objs = d.objects || {};
const lines = [];
for (const [key, v] of Object.entries(objs)) {
  if (!key.includes('LevelMaterial>')) continue;
  const mid = v.material;
  if (mid === undefined || mid === null) continue;
  const sps = v.shaderParams || [];
  const get = (n) => { for (const sp of sps) if (sp.name === n && sp.tex) return sp.tex; return ''; };
  const diffuse = get('u_diffuse1Tex') || get('u_diffuseTex');
  const diffuse2 = get('u_diffuse2Tex');
  const light = get('u_lightTex');
  const norm = get('u_normTex');
  lines.push(`MAT ${mid} ${diffuse} ${diffuse2} ${light} ${norm} ${(v.name || '').replace(/\s+/g, '_')}`);
}
fs.writeFileSync(output, lines.join('\n') + '\n');
console.log(`材质绑定 ${lines.length} 条 -> ${output}`);
