const express = require('express')
const app = express()
const port = 3000
const api = express.Router()
const { spawn, execFile } = require('child_process');
const { getmetas, writemeta, deleteentry, getMeta } = require('./main_join_meta');
const { LineBuffer } = require('./line-buffer');
const { genStrap } = require('./strap');
const { promisify } = require('util');
const fs = require('fs');
const exists = promisify(fs.exists);
const mkdir = promisify(fs.mkdir);
const readdir = promisify(fs.readdir);
const rename = promisify(fs.rename);
const writeFile = promisify(fs.writeFile);
const readFile = promisify(fs.readFile);
const path = '/mnt/music videos';
const { getMetaForFilename, queryMeta } = require('./meta');

const ProcessingQue = class {
  que = [];
  running = false;
  constructor(process) {
    this.process = process;
  }
  add(x) {
    this.que.push(x);
    this.start();
  }
  start() {
    if (this.running || !this.que.length) return;
    this.running = true;
    this.process(this.que[0]).then(() => {
      this.running = false;
      this.que.shift();
      this.start();
    });
  }
}

async function pingPlayer() {
  const pid = parseInt(await readFile("/tmp/.mpv.pid"));
  if (pid) process.kill(pid, 'SIGHUP');
}

api.use(express.json())
api.get('/list', async (req, res) => {
//  res.set('Cache-Control', 'max-age=0');
  console.log('listed')
  console.time('listed')
  res.json(await getmetas());
  console.timeEnd('listed')
});
api.get('/video/:id/play', async (req, res) => {
  console.log('play', req.params.id);
  await writeFile("/tmp/.mpv.playnow", path + "/" + req.params.id);
  await pingPlayer();
  res.json({});
})
api.get('/video/:id', async (req, res) => {
  console.log('detail', req.params.id);
  res.json(await getMeta(req.params.id));
})
api.get('/query/:q', async (req, res) => {
  console.log('query', req.params.q);
  res.json(await queryMeta(req.params.q))
});
api.post('/update', (req, res) => {
  console.log('updated', req.body);
  writemeta(req.body.id, req.body.meta);
  res.json({});
});
api.post('/delete', (req, res) => {
  console.log('deleted', req.body);
  deleteentry(req.body.id);
  res.json({});
});
function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}
let downloadCurrent;
let downloadName;
const downloadQue = new ProcessingQue(async (name) => {
  downloadCurrent = 0;
  downloadName = undefined;
  try {
    console.log("Downloading started", name);
    const tmpDir = path + '/.temp';
    if (!await exists(tmpDir)) {
      await mkdir(tmpDir);
    }
    const youtubeDl = spawn('../youtube-dl', [ `-o${tmpDir}/%(title)s.%(ext)s`, '-f137+251/136+251/135+251/best', '--newline', name]);
    youtubeDl.stdout.on('data', data => {
      const line = data.toString();
      const [ , pct ] = line.match(/\b(\d{1,3}(\.\d+))%/) || [];
      if (pct) downloadCurrent = parseFloat(pct);
      const [ , , target ] = line.match(/\bDestination: (.*)\/(.*)/) || [];
      if (target) downloadName = target;
    });
    await new Promise(resolve => youtubeDl.on('exit', resolve));
    for (const file of await readdir(tmpDir)) {
      await rename(tmpDir + '/' + file, path + '/' + file);
    }
  } catch (e) {
    console.log("error", e);
  }
  downloadCurrent = 0;
  downloadName = undefined;
})
api.post('/download', (req, res) => {
  console.log('download', req.body.urls);
  for (const url of req.body.urls) downloadQue.add(url);
  res.json({});
})
api.get('/download', (req, res) => {
  res.json(downloadQue.que.map((url, i) => ({
    url,
    name: i ? undefined : downloadName,
    progress: i ? 0 : downloadCurrent,
  })));
})

app.use('/api', api);
app.use('/', express.static('../webui/dist'));
app.use('/videos', express.static(path));

app.listen(port, () => console.log(`Example app listening at http://localhost:${port}`));

const inotify = spawn('./inotify', [path]);
const lb = new LineBuffer();
const que = new ProcessingQue(async (name) => {
  console.log("generate strap for", name);
  await genStrap(name);
  console.log("strap gen OK");
})
const metaQue = new ProcessingQue(async (name) => {
  console.log("generate meta for", name);
  await getMetaForFilename(path, name);
  console.log("meta done");
})
const channelsQue = new ProcessingQue(async () => {
  console.log("(re-)generating channels.txt");
  const files = (await readdir(path, { withFileTypes: true }))
    .filter(f => f.isFile() && f.name.match(/\.(mkv|mp4|mov)$/))
    .map(f => path + "/" + f.name + "\n");
  const content = "#1\n" + files.join("");
  await writeFile("../channels.txt", content);
  await pingPlayer();
  console.log("regenerating done");
});
inotify.stdout.on('data', data => lb.feed(data).map(line => JSON.parse(line)).forEach(ev => {
  console.log(ev);
  if (ev.name.endsWith('.meta.json') && ev.type == '>') que.add(ev.name);
  if ((ev.name.endsWith('.mkv') || ev.name.endsWith('.mp4') || ev.name.endsWith('.mov')) && ev.type == '>')
    metaQue.add(ev.name);
  if (ev.name.endsWith('.mkv') || ev.name.endsWith('.mp4') || ev.name.endsWith('.mp4'))
    channelsQue.add();
}));
