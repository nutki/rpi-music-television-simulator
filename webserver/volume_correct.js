#!/usr/bin/node
const { spawn } = require("child_process");
const { UnixDgramSocket } = require('unix-dgram-socket');

const commSocket = '/tmp/.mpv.socket';
function pingPlayer(msg) {
  try {
    const socket = new UnixDgramSocket();
    socket.send(msg, commSocket);
    socket.close();
  } catch(e) {
    console.log(e);
  }
}

console.log(process.argv)

const fileName = process.argv[2];
const youtubeDl = spawn("ffmpeg", [ '-i', fileName, '-filter:a', 'volumedetect', '-f', 'null', '-vn', '/dev/null']);
let duration = 0;
youtubeDl.stderr.on('data', data => {
  const line = data.toString();
  let [ , dh, dm, ds] = line.match(/DURATION\s*:\s*(\d+):(\d+):(\d+)/i) || [];
  if (dh) duration = (parseInt(dh)*60+parseInt(dm))*60+parseInt(ds);
  [ , dh, dm, ds ] = line.match(/time=(\d+):(\d+):(\d+)/) || [];
  if (dh) {
    const progress = (parseInt(dh)*60+parseInt(dm))*60+parseInt(ds);
    pingPlayer(`DVOLUME DETECT ${Math.floor(progress*100/(duration || 1))}%`);
  }
  const [ , volume ] = line.match(/\bmax_volume: (.*?) dB/) || [];
  if (volume) {
    correction = Math.floor(parseFloat(volume)*100);
    if (correction <= 0) pingPlayer(`V${-correction}`);
    console.log("result", correction);
  }
});
