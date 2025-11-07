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
const unlink = promisify(fs.unlink);
const { getMetaForFilename, queryMeta } = require('./meta');
const { UnixDgramSocket } = require('unix-dgram-socket');

const commSocket = '/tmp/.mpv.socket';
const ytdPath = './yt-dlp';
const videosPath = './videos';
const webuiPath = 'webui/dist';
const channelsPath = "channels";

const inotifyPath = require.resolve('./inotify');

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

async function pingPlayer(msg) {
  try {
    const socket = new UnixDgramSocket();
    socket.send(msg, commSocket);
    socket.close();
  } catch(e) {
    console.log(e);
  }
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
  await pingPlayer("P" + videosPath + "/" + req.params.id);
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
api.get('/channel', async (req, res) => {
  res.json(await channels_list());
});
api.get('/channel/:id', async (req, res) => {
  res.json(await channels_get_entries(req.params.id));
});
api.post('/channel/:id', async (req, res) => {
  await channels_set_entries(req.params.id, req.body.name, req.body.entries);
  res.json({});
});
api.delete('/channel/:id', async (req, res) => {
  await channels_delete(req.params.id);
  res.json({});
});
api.get('/press/:id', async (req, res) => {
  pingPlayer('K' + req.params.id);
  res.json({});
})

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}
let downloadCurrent;
let downloadName;

spawn(ytdPath, [ '--update' ]);

const downloadQue = new ProcessingQue(async (name) => {
  downloadCurrent = 0;
  downloadName = undefined;
  try {
    console.log("Downloading started", name);
    const tmpDir = videosPath + '/.temp';
    if (!await exists(tmpDir)) {
      await mkdir(tmpDir);
    }
    const youtubeDl = spawn(ytdPath, [ `-o${tmpDir}/%(title)s.%(ext)s`, '-f137+140/299+140/136+140/298+140/135+140/best', '--write-sub', '--sub-langs', 'en.*', '--sub-format', 'srt/best', '--convert-subs', 'srt', '--newline', name]);
    youtubeDl.stdout.on('data', data => {
      const line = data.toString();
      const [ , pct ] = line.match(/\b(\d{1,3}(\.\d+))%/) || [];
      if (pct) downloadCurrent = parseFloat(pct);
      const [ , , target ] = line.match(/\bDestination: (.*)\/(.*)/) || [];
      if (target) downloadName = target;
    });
    await new Promise(resolve => youtubeDl.on('exit', resolve));
    for (const file of await readdir(tmpDir)) {
      const dstfile = file.replace(/\.en[^.]*\.srt/, '.srt');
      await rename(tmpDir + '/' + file, videosPath + '/' + dstfile);
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
app.use('/', express.static(webuiPath));
app.use('/videos', express.static(videosPath));

app.listen(port, () => console.log(`Example app listening at http://localhost:${port}`));

const inotify = spawn(inotifyPath, [videosPath]);
const lb = new LineBuffer();
const que = new ProcessingQue(async (name) => {
  console.log("generate strap for", name);
  await genStrap(name);
  console.log("strap gen OK");
})
const metaQue = new ProcessingQue(async (name) => {
  console.log("generate meta for", name);
  await getMetaForFilename(videosPath, name);
  console.log("meta done");
})
const channelsQue = new ProcessingQue(async () => {
  console.log("(re-)generating channels.txt");
  const files = (await readdir(videosPath, { withFileTypes: true }))
    .filter(f => f.isFile() && f.name.match(/\.(mkv|mp4|mov)$/))
  await writeFile(channelsPath + "/0.txt", "\n" + files.map(f => f.name + "\n").join(""));
  await pingPlayer("C0");
  console.log("regenerating done");
});
inotify.stdout.on('data', data => lb.feed(data).map(line => JSON.parse(line)).forEach(ev => {
  console.log(ev);
  if (ev.name.endsWith('.meta.json') && ev.type == '>') que.add(videosPath + "/" + ev.name);
  if ((ev.name.endsWith('.mkv') || ev.name.endsWith('.mp4') || ev.name.endsWith('.mov')) && ev.type == '>')
    metaQue.add(ev.name);
  if (ev.name.endsWith('.mkv') || ev.name.endsWith('.mp4') || ev.name.endsWith('.mov'))
    channelsQue.add();
}));

const channels_list = async () => {
  const channels = {};
  for (const file of await readdir(channelsPath)) {
    const [m, nr] = file.match(/^(\d+).txt$/);
    if (m) {
      const contents = (await readFile(channelsPath + "/" + m)).toString().split("\n");
      channels[nr] = { name: contents[0] || undefined, length: contents.length - 2 };
    }
  }
  return channels;
}
const channels_get_entries = async (nr) => {
  const channel = { entries: [], name: "" };
  if (!await exists(channelsPath + "/" + nr + ".txt")) return channel;
  const contents = (await readFile(channelsPath + "/" + nr + ".txt")).toString().split("\n");
  channel.name = contents.shift();
  for (const line of contents) if (line) {
    channel.entries.push(line);
  }
  return channel;
}
const channels_set_entries = async (nr, name, entries) => {
  const contents = name + "\n" + entries.map(x => x + "\n").join("");
  await writeFile(channelsPath + "/" + nr + ".txt", contents);
  pingPlayer("C" + nr);
}
const channels_delete = async (nr) => {
  if (await exists(channelsPath + "/" + nr + ".txt")) await unlink(channelsPath + "/" + nr + ".txt");
}
