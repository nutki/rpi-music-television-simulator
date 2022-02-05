const fs = require("fs");
const { promisify } = require('util');
const exists = promisify(fs.exists);
const path = '/mnt/music videos';
const { spawn } = require('child_process');
const { LineBuffer } = require('./line-buffer');
const { inferMetaFromId } = require('./meta');

function readmeta(id) {
  const metaFileName = path + "/" + id + ".meta.json";
  try {
    return JSON.parse(fs.readFileSync(metaFileName));
  } catch (e) {
    return inferMetaFromId(id);
  }
}

async function getmetas() {
  const files = fs.readdirSync(path, { withFileTypes: true }).filter(f => f.isFile());
  const metas = [];
  for (const f of files) {
    const [, name] = f.name.match(/(.*)\.(mkv|mp4|mov)$/) || [];
    if (name) {
        const meta = readmeta(name);
        const stat = fs.statSync(path + "/" + f.name);
        metas.push({ id: name, meta, filename: f.name, videoTimeStamp: stat.ctimeMs });
    }
  };
  return metas;
}
function writemeta(id, meta) {
  const file = fs.writeFileSync(path + "/" + id + ".meta.json", JSON.stringify(meta, null, 2));
}
function deleteentry(id) {
  if (!id) return;
  const files = fs.readdirSync(path, { withFileTypes: true }).filter(f => f.isFile());
  for (const f of files) if (f.name.startsWith(id + ".")) {
    const fileName = path + "/" + f.name;
    fs.unlinkSync(fileName);
    console.log("remove", fileName);
  }
}
async function getMeta(id) {
  const r = { id };
  r.meta = readmeta(id);
  let video;
  if (await exists(path + "/" + id + ".mkv")) video = id + ".mkv";
  else if (await exists(path + "/" + id + ".mp4")) video = id + ".mp4";
  else if (await exists(path + "/" + id + ".mov")) video = id + ".mov";
  r.video = video;
  if (video) {
    const stat = await fs.promises.stat(path + "/" + video);
    console.log(stat);
    r.videoTimeStamp = stat.ctimeMs;
    r.videoSize = stat.size;
    const ffprobe = spawn('ffprobe', [ '-hide_banner', path + "/" + video ]);
    const buf = new LineBuffer();
    ffprobe.stderr.on('data', data => {
      for (line of buf.feed(data)) {
        const [ m1, widthStr, heightStr ] = line.match(/^ *Stream .*? Video: .*?\b(\d{2,6})x(\d{2,6})\b/) || [];
        if (m1) {
          r.videoWidth = parseInt(widthStr);
          r.videoHeight = parseInt(heightStr);
        }
        const [ m2, h, m, s, , br ] = line.match(/^ *Duration: (\d+):(\d+):(\d+(\.\d+)?).*?bitrate: (\d+)/) || [];
        if (m2) {
          r.videoLength = (parseInt(h) * 60 + parseInt(m)) * 60 + parseFloat(s);
          r.videoBitrate = parseInt(br)
        }
      }
    });
    await new Promise(resolve => ffprobe.on('exit', resolve));
  }
  console.log(r);
  return r;
}
module.exports = {
  getmetas,
  writemeta,
  deleteentry,
  getMeta,
}
