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
const youtubeDl = spawn("ffmpeg", [ '-skip_frame' , 'nokey', '-i', fileName, '-vf', 'cropdetect=20:2:0', '-f', 'null', '-']);
let duration = 0;
let result = "";
youtubeDl.stderr.on('data', data => {
  const line = data.toString();
  let [ , dh, dm, ds] = line.match(/DURATION\s*:\s*(\d+):(\d+):(\d+)/i) || [];
  if (dh) duration = (parseInt(dh)*60+parseInt(dm))*60+parseInt(ds);
  [ , dh, dm, ds ] = line.match(/time=(\d+):(\d+):(\d+)/) || [];
  if (dh) {
    const progress = (parseInt(dh)*60+parseInt(dm))*60+parseInt(ds);
    console.log("progress", Math.ceil(progress*100/(duration || 1)));
   pingPlayer(`DCROP DETECT ${Math.ceil(progress*100/(duration || 1))}%`);
  }
  const [ , correction ] = line.match(/\bcrop=(\d+:\d+:\d+:\d+)/) || [];
  if (correction) {
    result = correction;
  }
});
youtubeDl.stderr.on('end', () => {
  if (result) pingPlayer(`B${result}`);
  console.log("result", result);
});
