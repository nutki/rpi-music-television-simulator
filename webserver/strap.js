const fs = require("fs");
const util = require('util');
const execFile = util.promisify(require('child_process').execFile);

function hashCode(str) {
  let hash = 0;
  for (let i = 0; i < str.length; i++)
    hash  = ((hash << 5) - hash) + str.charCodeAt(i) | 0;
  return hash;
};

const template = fs.readFileSync(require.resolve("./template_v3.svg"), "utf8");
const TMP_FILE = "/tmp/.mpv.strap.svg";
const chromeName = 'chromium-browser'; // 'google-chrome';
function encodeXmlEntities(str) {
  return String(str).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}
async function genStrapSvg(meta, output) {
  const vars = {
    hash8: hashCode(meta.name) & 7,
    NAME: meta.name.toUpperCase(),
    ALBUM: meta.album.toUpperCase(),
    ARTIST: meta.artist.toUpperCase(),
    ...meta,
  };
  console.log(vars);
  const svg = template.replace(/\$\{(.*?)\}/g, (_, b) => encodeXmlEntities(vars[b]));
  fs.writeFileSync(TMP_FILE, svg);
  await execFile(chromeName,
    [ '--headless', '--window-size=1920,1080', '--default-background-color=0', `--screenshot=${output}`, TMP_FILE]);
}
async function genStrap(fileName) {
  const [, name] = fileName.match(/(.*)\.meta.json/) || [];
  try {
    const strapFileName = name + '.strap.png';
    const meta = JSON.parse(fs.readFileSync(fileName));
    console.log(name, strapFileName);
    await genStrapSvg(meta, strapFileName);
  } catch (e) {
    console.log(e);
  }
}
module.exports = {
  genStrap
}
