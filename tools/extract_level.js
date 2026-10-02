// 完整实例提取: tex回退链 + diffuseColor/chamColor + selfLit + isCloud + matrix16
const fs = require('fs');
const input = process.argv[2];
const output = process.argv[3];
const d = JSON.parse(fs.readFileSync(input, 'utf8'));
const objs = d.objects || {};
const lines = [];
let n = 0;
// Water 水面节点: transform.matrix → 位置+范围 (每关官方数值)
for (const [key, v] of Object.entries(objs)) {
  if (!key.includes('Water>')) continue;
  const tf = v.transform && v.transform.matrix;
  if (!tf || tf.length !== 4) continue;
  const pos = [tf[3][0], tf[3][1], tf[3][2]];
  const sx = Math.hypot(tf[0][0], tf[1][0], tf[2][0]);
  const sy = Math.hypot(tf[0][1], tf[1][1], tf[2][1]);
  const sz = Math.hypot(tf[0][2], tf[1][2], tf[2][2]);
  lines.push(`WATER ${pos[0].toFixed(3)} ${pos[1].toFixed(3)} ${pos[2].toFixed(3)} ${sx.toFixed(3)} ${sz.toFixed(3)} ${v.renderSurface !== undefined ? v.renderSurface : 1}`);
}
for (const [key, v] of Object.entries(objs)) {
  if (!key.includes('LevelMesh>')) continue;
  const sps = v.shaderParams || [];
  const getTex = (name) => { for (const sp of sps) if (sp.uniformName === name && sp.texValue) return sp.texValue; return null; };
  const getVec = (name) => { for (const sp of sps) if (sp.uniformName === name && sp.vecValue) { const w = sp.vecValue; return w; } return null; };
  let tex = getTex('u_diffuse1Tex') || getTex('u_diffuseTex') || getTex('u_diffuse2Tex') || '';
  const tex2 = getTex('u_diffuse2Tex') || '';
  const col = getVec('u_diffuseColor') || getVec('u_chamColor') || null;
  const lit = getVec('u_selfLit') || null;
  const cham = getVec('u_chamAmount') || null;
  const isCloud = v.isCloud ? '1' : '';
  // 矩阵: transform.matrix 4x4 嵌套数组 → 16 行主序
  const tf = v.transform && v.transform.matrix;
  let mat = [];
  if (tf && Array.isArray(tf) && tf.length === 4) {
    for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) mat.push(tf[r][c]);
  }
  if (mat.length !== 16) continue;
  const name = v.resourceName || key.split('>')[1] || ('inst' + n);
  const cstr = col ? `${col.x},${col.y},${col.z}` : '';
  const lstr = lit ? `${lit.x}` : '';
  const mstr = mat.map((x) => (Math.abs(x) < 1e-9 ? 0 : +x.toFixed(6))).join(',');
  lines.push(`[${n}] ${name} | shader=${v.shaderName || ''} | tex=${tex} | tex2=${tex2} | col=${cstr} | lit=${lstr} | cloud=${isCloud}`);
  lines.push(`mat=[${mstr}]`);
  n++;
}
// LevelMaterial 材质绑定 (地形顶点材质ID → 官方贴图名)
for (const [key, v] of Object.entries(objs)) {
  if (!key.includes('LevelMaterial>')) continue;
  const mid = v.material;
  if (mid === undefined || mid === null) continue;
  const sps = v.shaderParams || [];
  const get = (nm) => { for (const sp of sps) if (sp.name === nm && sp.tex) return sp.tex; return ''; };
  const diffuse = get('u_diffuse1Tex') || get('u_diffuseTex');
  lines.push(`MAT ${mid} ${diffuse}`);
}
fs.writeFileSync(output, lines.join('\n') + '\n');
console.log(`提取 ${n} 实例 -> ${output}`);
