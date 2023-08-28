const { spawn } = require('child_process');
const { readFileSync } = require('fs');

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
const fileContent = readFileSync('P101-0003.t42');

const printAt = (str, x, y) => {
  let pos = y * 40 + x + 2;
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
  for (let i = 0;; i++) {
   printAt(YELLOW + "MTVText", 11, 0);
   addTime();
    await sendTeletext(fileContent);
  }
  childProcess.stdin.end();
}

main();

function generateBinaryPacket() {
  return Buffer.alloc(42); // Create a new buffer of specified length
}
