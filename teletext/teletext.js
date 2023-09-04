const { spawn } = require('child_process');
const { readFileSync } = require('fs');
const { exit } = require('process');
const { charMap, x26CharMap } = require('./unicode');

const parity = [
  0x80,0x01,0x02,0x83,0x04,0x85,0x86,0x07,0x08,0x89,0x8A,0x0B,0x8C,0x0D,0x0E,0x8F,
  0x10,0x91,0x92,0x13,0x94,0x15,0x16,0x97,0x98,0x19,0x1A,0x9B,0x1C,0x9D,0x9E,0x1F,
  0x20,0xA1,0xA2,0x23,0xA4,0x25,0x26,0xA7,0xA8,0x29,0x2A,0xAB,0x2C,0xAD,0xAE,0x2F,
  0xB0,0x31,0x32,0xB3,0x34,0xB5,0xB6,0x37,0x38,0xB9,0xBA,0x3B,0xBC,0x3D,0x3E,0xBF,
  0x40,0xC1,0xC2,0x43,0xC4,0x45,0x46,0xC7,0xC8,0x49,0x4A,0xCB,0x4C,0xCD,0xCE,0x4F,
  0xD0,0x51,0x52,0xD3,0x54,0xD5,0xD6,0x57,0x58,0xD9,0xDA,0x5B,0xDC,0x5D,0x5E,0xDF,
  0xE0,0x61,0x62,0xE3,0x64,0xE5,0xE6,0x67,0x68,0xE9,0xEA,0x6B,0xEC,0x6D,0x6E,0xEF,
  0x70,0xF1,0xF2,0x73,0xF4,0x75,0x76,0xF7,0xF8,0x79,0x7A,0xFB,0x7C,0xFD,0xFE,0x7F];
const hamming84 = [0x15,0x02,0x49,0x5E,0x64,0x73,0x38,0x2F,0xD0,0xC7,0x8C,0x9B,0xA1,0xB6,0xFD,0xEA];
const unhamming84 = x => hamming84.indexOf(x);
const hamming18_0 = [0x0b,0x0c,0x12,0x15,0x21,0x26,0x38,0x3f,0x40,0x47,0x59,0x5e,0x6a,0x6d,0x73,0x74];
const hamming18_1b = [
  0x0,0x1,0x2,0x3,0x3,0x2,0x1,0x0,0x8,0x9,0xa,0xb,0xb,0xa,0x9,0x8,
  0x9,0x8,0xb,0xa,0xa,0xb,0x8,0x9,0x1,0x0,0x3,0x2,0x2,0x3,0x0,0x1,
  0xa,0xb,0x8,0x9,0x9,0x8,0xb,0xa,0x2,0x3,0x0,0x1,0x1,0x0,0x3,0x2,
  0x3,0x2,0x1,0x0,0x0,0x1,0x2,0x3,0xb,0xa,0x9,0x8,0x8,0x9,0xa,0xb,
  0xb,0xa,0x9,0x8,0x8,0x9,0xa,0xb,0x3,0x2,0x1,0x0,0x0,0x1,0x2,0x3,
  0x2,0x3,0x0,0x1,0x1,0x0,0x3,0x2,0xa,0xb,0x8,0x9,0x9,0x8,0xb,0xa,
  0x1,0x0,0x3,0x2,0x2,0x3,0x0,0x1,0x9,0x8,0xb,0xa,0xa,0xb,0x8,0x9,
  0x8,0x9,0xa,0xb,0xb,0xa,0x9,0x8,0x0,0x1,0x2,0x3,0x3,0x2,0x1,0x0,
];

const parity32 = v => {
  const p16 = v ^ (v >> 16);
  const p8 = p16 ^ (p16 >> 8);
  const p4 = p8 ^ (p8 >> 4);
  const p2 = p4 ^ (p4 >> 2);
  const p1 = p2 ^ (p2 >> 1);
  return p1 & 1;
}

const makeMask = a => {
  return a.reduce((a, c) => a | (1 << (c-1)), 0);
}
const p1mask = makeMask([1, 2, 4, 5, 7, 9, 11, 12, 14, 16, 18]);
const p2mask = makeMask([1, 3, 4, 6, 7, 10, 11, 13, 14, 17, 18]);
const p3mask = makeMask([2, 3, 4, 8, 9, 10, 11, 15, 16, 17, 18]);
const p4mask = makeMask([5, 6, 7, 8, 9, 10, 11]);
const p5mask = makeMask([12, 13, 14, 15, 16, 17, 18])
const specHamming18 = v => {
  const p1 = 1^parity32(v & p1mask);
  const p2 = 1^parity32(v & p2mask);
  const p3 = 1^parity32(v & p3mask);
  const p4 = 1^parity32(v & p4mask);
  const p5 = 1^parity32(v & p5mask);
  const d1 = v & 1;
  const d2 = (v >> 1) & 1;
  const d3 = (v >> 2) & 1;
  const d4 = (v >> 3) & 1;
  const d5_11 = (v >> 4) & 0x7f;
  const d12_18 = (v >> 11) & 0x7f;
  const byte0 = p1 | (p2 << 1) | (d1 << 2) | (p3 << 3) | (d2 << 4) | (d3 << 5) | (d4 << 6) | (p4 << 7);
  const res = byte0 | (d5_11 << 8) | (p5 << 15) | (d12_18 << 16);
  return res | ((1^parity32(res)) << 23);
}
const myHamming18 = v => {
  const byte1 = (v >> 4) & 0x7f;
  const byte2 = (v >> 11) & 0x7f;
  const byte0 = hamming18_0[v & 0xf] ^ hamming18_1b[byte1 ^ byte2];
  const p4 = parity[byte1] & 0x80;
  const p5 = parity[byte2] & 0x80;
  const p6 = parity[byte0] & 0x80;
  return (byte0 | p4) | ((byte1 | p5) << 8) | ((byte2 | p6) << 16);
}


for (let i = 0; i < (1 << 18); i++) {
  const j = i;
  const v1 = myHamming18(j);// & 0x7f7f74;
  const v2 = specHamming18(j);//  & 0x7f7f74;
  if (v1 != v2) {
    console.log((j).toString(16).padStart(5,'0'), (v1).toString(16).padStart(6,'0'), (v2).toString(16).padStart(6,'0'), (v1^v2).toString(16).padStart(6,'0'));
    exit(0);
  }
}

const writeHamming18 = (buf, index, v) => {
  const byte1 = (v >> 4) & 0x7f;
  const byte2 = (v >> 11) & 0x7f;
  const byte0 = hamming18_0[v & 0xf] ^ hamming18_1b[byte1 ^ byte2];
  const p4 = parity[byte1] & 0x80;
  const p5 = parity[byte2] & 0x80;
  const p6 = parity[byte0] & 0x80;
  buf.writeUInt8(byte0 | p4, index * 3 + 3);
  buf.writeUInt8(byte1 | p5, index * 3 + 4);
  buf.writeUInt8(byte2 | p6, index * 3 + 5);
};

function sleep(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}
const sendTeletext = async (buffer) => {
  childProcess.stdin.write(buffer);
  const lines = buffer.length/42;
  const band = 32 * 25;
  await sleep(1000/band * lines);
}

// Create the child process
const childProcess = spawn('../raspi-teletext/teletext', ['-']);
const fileContent = readFileSync('P101-0005.t42');

const printAt = (str, x, y) => {
  let pos = y * 42 + x + 2;
  for (const ch of str) {
    fileContent.writeUInt8(parity[ch.charCodeAt(0)], pos++);
  }
}


// Handle child process events
childProcess.on('error', (error) => {
  console.error(`Child process error: ${error.message}`);
});

childProcess.on('exit', (code, signal) => {
  console.log(`Child process exited with code ${code} and signal ${signal}`);
});

process.on('SIGTERM',function(){
  process.exit(1);
});

// process.on('disconnect', function() {
//   console.log('parent exited')
//   process.exit();
// });
const BLACK = "\0";
const RED = "\x01";
const GREEN = "\x02";
const YELLOW = "\x03";
const BLUE = "\x04";
const MAGENTA = "\x05";
const CYAN = "\x06";
const WHITE = "\x07";
const FLASH = "\x08";
const STEADY = "\x09";
const END_BOX = "\x0A";
const START_BOX = "\x0B";
const NORMAL_SIZE = "\x0C";
const DOUBLE_HEIGHT = "\x0D";
const DOUBLE_WIDTH = "\x0E";
const DOUBLE_SIZE = "\x0F";
const MOSAIC_BLACK = "\x10";
const MOSAIC_RED = "\x11";
const MOSAIC_GREEN = "\x12";
const MOSAIC_YELLOW = "\x13";
const MOSAIC_BLUE = "\x14";
const MOSAIC_MAGENTA = "\x15";
const MOSAIC_CYAN = "\x16";
const MOSAIC_WHITE = "\x17";
const CONCEAL = "\x18";
const CONTIGUOUS = "\x19";
const SEPARATED_MOSAIC = "\x1A";
const ESC = "\x1B";
const BLACK_BACKGROUND = "\x1C";
const NEW_BACKGROUND = "\x1D";
const HOLD_MOSAICS = "\x1E";
const RELEASE_MOSAICS = "\x1F";
const addTime = () => {
  const date = new Date();
  const h = date.getHours();
  const m = date.getMinutes();
  const s = date.getSeconds();
  const time = RED + `${h/10|0}${h%10}:${m/10|0}${m%10}:${s/10|0}${s%10}`;
  printAt(time, 31, 0);
  const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  const days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const formattedDate = `${days[date.getDay()]} ${date.getDate()} ${months[date.getMonth()]}`.padStart(10);
  printAt(formattedDate, 21, 0);
}

async function main() {
  fileContent.writeUInt8(hamming84[0], 2);
  const y27 = generateBinaryPacket();
  const y26 = generateBinaryPacket();
  const y26b = generateBinaryPacket();
  setPacketAddress(y27, 0, 1, 27, 0);
  setPacketAddress(y26, 0, 1, 26, 0);
  writeHamming18(y26, 0, 40 + 22 + ((1) << 6) + ((4) << 11));
//  writeHamming18(y26, 1, 42 + ((0) << 6) + ((3) << 11));
  // writeHamming18(y26, 2, 10 + ((15) << 6) + ((0x2C) << 11));
  // writeHamming18(y26, 3, 11 + ((15) << 6) + ((0x78) << 11));
  // writeHamming18(y26, 4, 12 + ((15) << 6) + ((0x24) << 11));
  // writeHamming18(y26, 5, 13 + ((15) << 6) + ((0x55) << 11));
  const letter = 'z'.codePointAt(0);
  writeHamming18(y26, 1, 14 + ((16+1) << 6) + ((letter) << 11));
  writeHamming18(y26, 2, 15 + ((16+2) << 6) + ((letter) << 11));
  writeHamming18(y26, 3, 16 + ((16+3) << 6) + ((letter) << 11));
  writeHamming18(y26, 4, 17 + ((16+4) << 6) + ((letter) << 11));
  writeHamming18(y26, 5, 18 + ((16+5) << 6) + ((letter) << 11));
  writeHamming18(y26, 6, 19 + ((16+6) << 6) + ((letter) << 11));
  writeHamming18(y26, 7, 20 + ((16+7) << 6) + ((letter) << 11));
  writeHamming18(y26, 8, 21 + ((16+8) << 6) + ((letter) << 11));
  writeHamming18(y26, 9, 22 + ((16+9) << 6) + ((letter) << 11));
  writeHamming18(y26, 10, 23 + ((16+10) << 6) + ((letter) << 11));
  writeHamming18(y26, 11, 24 + ((16+11) << 6) + ((letter) << 11));
  writeHamming18(y26, 12, 25 + ((16+12) << 6) + ((letter) << 11));
  setPacketAddress(y26b, 0, 1, 26, 1);
  writeHamming18(y26b, 0, 40 + 22 + ((1) << 6) + ((4) << 11));
  writeHamming18(y26b, 1, 26 + ((16+13) << 6) + ((letter) << 11));
  writeHamming18(y26b, 2, 27 + ((16+14) << 6) + ((letter) << 11));
  writeHamming18(y26b, 3, 28 + ((16+15) << 6) + ((letter) << 11));
  writeHamming18(y26b, 4, 29 + (x26CharMap['ź'] << 6));
  writeHamming18(y26b, 5, 30 + (x26CharMap['['] << 6));
  writeHamming18(y26b, 6, 31 + (x26CharMap['{'] << 6));
  writeHamming18(y26b, 7, 32 + (x26CharMap['$'] << 6));
  writeHamming18(y26b, 8, 33 + (x26CharMap['#'] << 6));
  for (let i = 0; i < 6; i++) {
    y27.writeUInt8(hamming84[i*2], i*6 + 3);
    y27.writeUInt8(hamming84[1], i*6 + 4);
    y27.writeUInt8(hamming84[0xf], i*6 + 5);
    y27.writeUInt8(hamming84[0x7], i*6 + 6);
    y27.writeUInt8(hamming84[0xf], i*6 + 7);
    y27.writeUInt8(hamming84[0x3], i*6 + 8);
  }
  y27.writeUInt8(hamming84[0xf], 39);
  y27.writeUInt8(hamming84[0x0], 40);
  y27.writeUInt8(hamming84[0x0], 41);
  for (let i = 0;; i++) {
    fileContent.writeUInt8(hamming84[i%30==1?8:0], 7); //subtitle
//    fileContent.writeUInt8(hamming84[i%30==1?8:0], 5); //erase
    fileContent.writeUInt8(hamming84[i%10], 2);
    fileContent.writeUInt8(hamming84[Math.floor(i%30/10)], 3);
    printAt(YELLOW + "MTVText"+i%10, 11, 0);
    printAt(START_BOX+START_BOX+"Hel_#$"+END_BOX+END_BOX, 15, 23);
    addTime();
//    console.log(fileContent.readUInt8(420), fileContent.readUInt8(421));
//    console.log(fileContent.readUInt8(420), fileContent.readUInt8(421));
    await sendTeletext(fileContent.subarray(0, fileContent.length - 0));
    await sendTeletext(y26);
    await sendTeletext(y26b);
    await sendTeletext(y27);
    console.log(i);
  }
  childProcess.stdin.end();
}

main();

function generateBinaryPacket() {
  return Buffer.alloc(42); // Create a new buffer of specified length
}
function setPacketAddress(buf, line, x, y, dc = undefined) {
  const b1 = hamming84[x + (y&1 ? 8 : 0)];
  const b2 = hamming84[y >> 1];
  buf.writeUInt8(b1, 0 + line * 42);
  buf.writeUInt8(b2, 1 + line * 42);
  if (dc !== undefined) buf.writeUInt8(hamming84[dc], 2 + line * 42)
}
