const { spawn } = require('child_process');
const { readFileSync } = require('fs');
const { exit } = require('process');
const { charMap, x26CharMap } = require('./unicode');
const readline = require('readline');
const rl = readline.createInterface(process.stdin);

function printBufferWithControlCharacters(buffer) {
  for (let i = 0; i < buffer.length; i++) {
    const byte = buffer[i]&0x7f;
    if (i % 42 === 0) process.stdout.write('"')
    if (i % 42 < 2) continue;
    if (byte == 34) {
      process.stdout.write('\\"');
    } else if (byte >= 32 && byte < 127) {
      process.stdout.write(String.fromCharCode(byte));
    } else {
      process.stdout.write(`\\x${byte.toString(16).padStart(2, '0')}`);
    }
    if (i % 42 === 41) process.stdout.write('",\n')
  }
  process.stdout.write('\n');
}


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
const sendTeletext = async (buffers) => {
  for (const buffer of buffers) childProcess.stdin.write(buffer);
  const lines = buffers.reduce((a, c) => a + c.length, 0)/42;
  const band = 32 * 25;
  const currentTime = new Date();
  const txTime = 1000/band * lines;
  if (sendTeletext.lastTime !== undefined) {
    const elapsedTimeMs = currentTime - sendTeletext.lastTime;
    if (txTime > elapsedTimeMs) await sleep(txTime - elapsedTimeMs);
  }
  sendTeletext.lastTime = new Date();
}

// Create the child process
const childProcess = spawn('../raspi-teletext/teletext', ['-']);
// const fileContent = readFileSync('P101-0004(1).t42');
// printBufferWithControlCharacters(fileContent);
// exit(0)
const newPage = (options = {}) => {
  const lines = [];
  const magazine = (options.magazine || 1) & 7;
  const subtitle = options.subtitle || false;
  const links = options.links || [];
  options.content?.forEach((str, i) => {
    if (str) {
      const buf = lines[i+1] = Buffer.alloc(42, 32);
      setPacketAddress(buf, 0, magazine, i+1);
      let pos = 2;
      for (const ch of str) buf.writeUInt8(parity[ch.codePointAt(0)], pos++);
    }
  });
  return {
    magazine,
    subtitle,
    lines,
    extraChars: [],
    links,
    clean: false,
    buffer: undefined,
  }
}
const getPageBuffer = (page) => {
  if (!page.clean) {
    const content = page.lines.filter((b, i) => b && i);
    let y26pos = 13, y26 = undefined, y26lasty, y26idx = 0;
    page.extraChars.forEach((ch, i) => {
      if (ch === undefined) return;
      const x = i%40;
      const y = i/40|0;
      let updatey = y26lasty !== y;
      if (y26pos > (updatey ? 11 : 12)) {
        if (y26idx === 16) return;
        y26 = generateBinaryPacket();
        setPacketAddress(y26, 0, page.magazine, 26, y26idx++);
        content.push(y26);
        y26pos = 0;
        updatey = true;
      }
      if (updatey) writeHamming18(y26, y26pos++, 40 + y + (4 << 6));
      writeHamming18(y26, y26pos++, x + (ch << 6));
      y26lasty = y;
    });
    if (page.links.length) {
      const y27 = generateBinaryPacket();
      setPacketAddress(y27, 0, page.magazine, 27, 0);
      for (let i = 0; i < 6; i++) {
        const addr = page.links[i] ? parseInt(page.links[i].toString(), 16) : 0xFF;
        const magOffset = page.links[i] ? (page.magazine ^ (page.links[i]/100)) & 7 : 0;
        y27.writeUInt8(hamming84[addr&0x0f], i*6 + 3);
        y27.writeUInt8(hamming84[(addr&0xf0)>>4], i*6 + 4);
        y27.writeUInt8(hamming84[0xf], i*6 + 5);
        y27.writeUInt8(hamming84[0x7 | (magOffset & 1 ? 8 : 0)], i*6 + 6);
        y27.writeUInt8(hamming84[0xf], i*6 + 7);
        y27.writeUInt8(hamming84[0x3 | ((magOffset >> 1) << 2)], i*6 + 8);
      }
      y27.writeUInt8(hamming84[0xf], 39);
      y27.writeUInt8(hamming84[0x0], 40);
      y27.writeUInt8(hamming84[0x0], 41);    
      content.push(y27);
    }
    page.buffer = Buffer.concat(content);
    page.clean = true;
  }
  return page.buffer;
};
const getPageLine = (page, y) => {
  if (!page.lines[y]) {
    const buf = page.lines[y] = Buffer.alloc(42, parity[32]);
    setPacketAddress(buf, 0, page.magazine, y);
  }
  return page.lines[y];
}
const pageErase = (page) => {
  page.lines = [];
  page.extraChars = [];
  page.clean = false;
};
const linePrintRawAt = (line, str, x) => {
  let pos = x + 2;
  for (const ch of str) line.writeUInt8(parity[ch in charMap ? charMap[ch] : ch.codePointAt(0)], pos++);
}

const pagePrintAt = (page, str, x, y) => {
  let pos = x + 2, extraPos = y * 40 + x;
  const line = getPageLine(page, y);
  for (const ch of str) {
    line.writeUInt8(parity[ch in charMap ? charMap[ch] : ch.codePointAt(0) < 128 ? ch.codePointAt(0) : 32], pos++);
    page.extraChars[extraPos++] = x26CharMap[ch];
  }
  page.clean = false;
}
const pagePrintRawAt = (page, str, x, y) => {
  let pos = x + 2, extraPos = y * 40 + x;
  const line = getPageLine(page, y);
  for (const ch of str) {
    line.writeUInt8(parity[ch.codePointAt(0)], pos++);
    page.extraChars[extraPos++] = undefined;
  }
  page.clean = false;
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

rl.on('line', processInput);

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
const headerAddTime = (buf) => {
  const date = new Date();
  const h = date.getHours();
  const m = date.getMinutes();
  const s = date.getSeconds();
  const time = RED + `${h/10|0}${h%10}:${m/10|0}${m%10}:${s/10|0}${s%10}`;
  linePrintRawAt(buf, time, 31);
  const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  const days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const formattedDate = `${days[date.getDay()]} ${date.getDate()} ${months[date.getMonth()]}`.padStart(10);
  linePrintRawAt(buf, formattedDate, 21);
}

const content = [];
const p100 = newPage({ content: [
"\x03MUSIC TELEVISION(R)",
"\x137######;o\x7f?######;\x7f",
"\x135      \"m&       ?*",
"\x135          \x14tx?   ~=\x17  x          x    ",
"\x135         \x14x\x7f!0  ~'x\x1d\x17n\x7f$x<|0|4|4n\x7f$   ",
"\x135      \x14x~'!\x7fju ~'x\x1d\x17 j\x7f \x7f=/%{=\x7f1j\x7f    ",
"\x135      \x14+!  \x7f\"\x7fz7h\x7f\x1d\x17 \"/$+-/!/%/%\"/$   ",
"\x135      }0 f\x14\x7f k\x7f\x13xh                    ",
"\x135      \x7fj|\x7f\x14\x7f5\"%\x13\x7fj",
"\x135      ?j\x7f\x7f\x14\"   \x13?j",
"\x13-,,,,,,,///,,,,,,,/",
" ",
" ",
" ",
" ",
" ",
" ",
" ",
" ",
" ",
" ",
" ",
" ",
"\x01MTV Today\x02UK Today \x03Charts   \x06Subtitles",
], links: [100,102,105,888]});

content[195] = newPage({ content: [
  "\x01\x1d\x07 MTV UK VIEWERS PLEASE SEE PAGE 170  ",
  "\x03MUSIC TELEVISION(R)  \x06M.JACKSON    \x03256",
  "\x137######;o\x7f?######;\x7f  \x06NO DOUBT     \x03257",
  "\x135      \"m&       ?*  \x06U2           \x03259",
  "\x135          \x14tx?   ~=\x17  x          x    ",
  "\x135         \x14x\x7f!0  ~'x\x1d\x17n\x7f$x<|0|4|4n\x7f$   ",
  "\x135      \x14x~'!\x7fju ~'x\x1d\x17 j\x7f \x7f=/%{=\x7f1j\x7f    ",
  "\x135      \x14+!  \x7f\"\x7fz7h\x7f\x1d\x17 \"/$+-/!/%/%\"/$   ",
  "\x135      }0 f\x14\x7f k\x7f\x13xh                    ",
  "\x135      \x7fj|\x7f\x14\x7f5\"%\x13\x7fj  \x07MTV Today    \x03102",
  "\x135      ?j\x7f\x7f\x14\"   \x13?j  \x07MTV Tomorrow \x03103",
  "\x13-,,,,,,,///,,,,,,,/  \x07Highlights   \x03110",
  "\x06Holland        \x03450                    ",
  "\x06United Kingdom \x03500  \x07MTV News     \x03140",
  "\x06Ireland        \x03520  \x07Competitions \x03150",
  "\x06Denmark        \x03550  \x07A-Z Index    \x03199",
  "\x06Germany        \x03600  \x07Latest Charts\x03210",
  "\x06Switzerland    \x03675   Tour Guide   \x03250",
  "\x06Sweden         \x03700                    ",
  "\x06Belgium        \x03750  \x07Advertising  \x03300",
  "\x06Norway         \x03800  \x07Study Europe \x03305",
  " ",
  " ",
  ]});

  content[199] = newPage({
  content:[
    "\x13<,,,lp  `,,,,t     \x06European Magazine  ",
    "\x135    +\x7f<!hx/ \x7f h?\x17\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
    "\x135     \" xo5x 'x?\x17\x7f\x7f\x7f\x7f?!/\x7f//\x7f/\x7f?o'*o\x7f\x7f\x7f\x7f",
    "\x135       aj5ot~'\x17\x7f\x7f\x7f\x7f\x7f} |5($jt\"`~4h~\x7f\x7f\x7f\x7f",
    "\x135   jt `nj5\"?a \x17\x7f\x7f\x7f\x7f\x7f\x7f0#u\"#k!x0ku\"k\x7f\x7f\x7f\x7f",
    "\x135   j\x7f)!j*!  \x7f \x17\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
    "\x13-,,,.!  *,,,,'   \x03MUSIC TELEVISION (R) ",
    "\x04\x1d\x06         Today's Headlines           ",
    "\x03Music\x07Love anger at Cobain T-shirt \x03152",
    "\x03Films\x07Caine under guard in Russia  \x03161",
    "\x03World\x07UN promises new Rwanda probe \x03171",
    "\x03Sport\x07Bagwell monster run continues\x03181",
    "\x01\x1d\x03 HELP STING SAVE THE RAINFOREST\x08198  ",
    "\x06Programmes   \x03110  \x06MTVtext Backup \x03191",
    "\x06Competitions \x03130  \x06A-Z Index      \x03199",
    "\x06MTV News     \x03140  \x06Magazine       \x03201",
    "\x06Music Index  \x03150  \x06Charts Update  \x03210",
    "\x06Movies/Videos\x03160  \x06Concert Guide  \x03251",
    "\x06World  News  \x03170  \x06Advertising    \x03301",
    "\x06Sports News  \x03180   NEW FONE FUN    401",
    "    \x04\x1d\x07  FONE FUN IRELAND GO\x08402   \x1c    ",
    "\x03\x1d\x1d\x01PLAY THE MTV EURO TOP 20 GAME 425   ",
    "\x03\x1d\x07IN AUG. EACH 50th CALLER WJNS T-SHIRT",
  ],
});
content[198] = newPage({content:[
  "\x02\x15\x02\x15\x15\x15\x15\x15                                ",
  "\x13  7###mp&###sk5`0\x03MUSIC TELEVISION(R)  ",
  "\x14\x1d\x135    ! p}'1*h?! \x17  `4         `4     ",
  "\x14\x1d\x135     /#\x7f*u`?   \x17 (\x7f=`|lth|h|(\x7f=     ",
  "\x14\x1d\x135   }0x \x7f o~!   \x17  \x7f5j\x7f./b\x7fnw \x7f5     ",
  "\x14\x1d\x135   \x7f\"j ? *%4   \x17  +-\"/.'*/*/ +-     ",
  "\x13  -,,,' *,,,,.!                        ",
]});
content[197] = newPage({content:[
  "\x11 k:26>7.245eijj4\x13og\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
  "\x11 u5ee5ue5e5?zjjj0\x13~\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f?",
  "\x115jjjj+w*ue=5%&::y\x13*\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f>t",
  "\x11+v\x17x}\x1e\x11mjj\x17~\x7f}~\x11i\x1f\x13+\x7f\x7fk?\x7f?\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
  "\x11u\x17z\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x1e\x116~ \x13\"/v/v/v/}+\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
  "\x11j\x17\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x1e\x11jj \x17`~\x7f\x1e\x7f\x7f\x7f?\x1d\x13\"o\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
  "\x11`\x17\x7f\x7f\x7f?/\x7f\x7f\x7f\x1e\x7f\x7f/{?\x116 \x17z\x7f\x7f\x7f\x7f\x7f\x7f?\x1d  \x13o\x7f\x7f\x1c\x7f\x7f7",
  "\x11*\x17\x7f\x7f\x7f?/|{\x7f\x1e\x7fyg;\x7f\x11j \x17\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x1d\x7f\x13j\x7f\x7f\x1c\x7f?!",
  "\x17  \x7f\x7f\x7f:w6\x7f\x1e\x7fji{e?\x11& \x17\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f?\x1d\x7f\x13~\x7f\x1c\x7f\x7f\x7f ",
  "\x17 vo\x7f\x7f\x7f|~\x7f\x7f\x7fj}|\x7f?}m  \x7f\x7f\x7f\x7f\x7f?\x7f\x7f?\x1d\x13h\x7f^\x1c\x7f\x7f\x7f ",
  "\x17 *z\x7f\x7f\x7f\x7fg{\x7f\x7fxvo\x7f\x7f\x7f>  *\x7f\x7f\x7fg~\x7f?\x1d\x7f\x13j\x7f\x7f^\x1c\x7f? ",
  "\x14u\x17*\x7f\x7f\x7f5w2\x7f\x7f725\x7f?5\x14\"\x17*~\x7f}.\x7f\x7f\x1e\x7f\x1d\x13j\x7f#o\x7f\x1c% ",
  "\x14wv\x17o\x7f\x7f\x7f~\x7fj\x7fh\x7f\x7f\x7f\x7f5\x14b\x17`voi\x7f6\x7f?\x1d\x13?   j\x7f\x7f\x1c ",
  "\x14\x7fy\x17*\x7f\x7f\x7f\x7f\x7f}|\x7f\x7f\x7f\x7f\x7f\x14(9\x17*>jj>y\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f?5  ",
  "\x14\x7fm:\x17\x7f\x7f\x7f\x7f'<l,6o\x7f5\x14f?0\x17>z}~\x7f\x7f?\x7f\x7f\x7f\x1d\x7f\x13j\x7f\x1c\x14:",
  "\x14\x7fw9\x17*\x7f\x7f)brqsr\"\x7f\x14d;<\x17<yw,vo\x7f\x7f\x7f\x7f\x7f\x7f\x1d\x7f\x13\x7f\x1c\x14z",
  "\x14\x7f>l9\x17o\x7f\x7f\x7f}||\x7f\x7f% \x14.5\x17~?-,.e\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f?5\x148k",
  "\x14\x7f{-(\x17\"\x7f\x7f?\x7f\x7f\x7fg?\x14(rfv0\x17`\x7f\x7f/sh4|+\x7f\x7f\x7f\x7f?5\x14fo",
  "\x14\x7f\x7f;i!\x17k\x7f\x7f|||\x7f%\x14df8r8\x17 ($cpppp0j\x7f\x7f\x7f?5\x14f\x7f",
  "\x14\x7fw{9!\x17 o\x7f\x7f\x7f\x7f\x7f\x14lnow}v1\x17h~\x7f\x7f\x7f\x7f\x7f\x7f~\x7f\x7f\x7f?5\x149{",
  "\x14\x7f=={9(\x17j\x7f\x7f\x7f\x7f\x7f\x14'faoyw|\x17 k\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f?5\x14$9",
  "\x14\x7fo{$99\x17 \x7f\x7f\x1e\x7f\x16` \x14m:smg} \x17##    +\x7f\x7f/\x16x\x7f|p",
  "\x14\x7fo{9(\x16x\x17\x7f\x1e\x7f/\x16\x7f\x1ft\x14fx}}yv,p\x16 h\x7f\x7f}||~\x1d\x042/3",
]});
content[196] = newPage({content:[
  "\x03MUSIC TELEVISION(R)  \x06WIN A HOLIDAY TO ",
  "\x137######;o\x7f?######;\x7f  \x06ANTIGUA WITH MTV ",
  "\x135      \"m&       ?*  \x06WORLD TOUR   \x03188",
  "\x135          \x14tx?  `~=  \x17x          x    ",
  "\x135        \x14`x\x7f!0 `~'x\x1d\x17n\x7f$x<|0|4|4n\x7f$   ",
  "\x135      \x14x~'!\x7fju ~'x\x1d \x17j\x7f \x7f=/%{=\x7f1j\x7f    ",
  "\x135      \x14+!  \x7f\"\x7fz7h\x1d  \x17\"/$+-/!/%/%\"/$   ",
  "\x135      }0`f\x14\x7f k\x7f\x13xh                    ",
  "\x135      \x7fj|\x7f\x14\x7f5\"%\x13\x7fj\x07  MTV Today    \x03102",
  "\x135      ?j\x7f\x7f\x14\"   \x13?j\x07  MTV Tomorrow \x03103",
  "\x13-,,,,,,,///,,,,,,,/\x07  Highlights   \x03110",
  "\x06Holland        \x03450                    ",
  "\x06United Kingdom \x03500  \x07MTV News     \x03140",
  "\x06Ireland        \x03520  \x03Competitions \x03150",
  "\x06Denmark        \x03550  \x07A-Z Index    \x03199",
  "\x06Germany        \x03600  \x07Latest Charts\x03210",
  "\x06Switzerland    \x03675  \x07Tour Guide   \x03250",
  "\x06Sweden         \x03700                    ",
  "\x06Belgium        \x03750  \x07Advertising  \x03300",
  "\x06Norway         \x03800  \x07Study Europe \x03305",
  "                                        ",
  "\x07\x1d\x04 BEAVIS & BUTT-HEAD DO AMERICA 358   ",
  "\x07\x1d\x01 +LOOKALIKE COMPETITION & TOUR BUS   ",
]});
content[100] = p100;
content[888] = newPage({subtitle:true, magazine:8});

const rss = [" After the anonymous artist Ghostwriter",
"went viral with their A.I.-generated",
"track “Heart on My Sleeve” — which",
"mimics Drake and The Weeknd — earlier",
"this year, representatives for the",
"unknown act recently disclosed in an",
"interview with The New York Times that",
"they submitted the controversial song",
"for next year’s Grammy awards.",
" Submitted for best rap song and song of",
"the year, “Heart on My Sleeve” was",
"eligible despite the use of A.I.",
"technology on the record, Harvey Mason,",
"jr., CEO of the Recording Academy,",
"told The New York Times. “As far as the",
"creative side, it’s absolutely eligible",
"because it was written by a human,” he",
"noted.",
" Billboard has reached out to Drake and",
"The Weeknd for comment.",
" Last April, “Heart on My Sleeve” was",
"pulled from streaming services after",
"generating more than 600,000 plays on",
]
content[200] = newPage({magazine:2});
rss.forEach((v, i) => pagePrintAt(content[200], v, 0, i+1));
let subs;
let playerPos = 0;

const doubleHeightSubs = true;
async function main() {
  const header = Buffer.alloc(42, parity[32]);
  header.writeUInt8(hamming84[0], 0);
  header.writeUInt8(hamming84[0], 1);
  header.writeUInt8(hamming84[0], 8); // C7-C10
  header.writeUInt8(hamming84[1], 9); // C11-C14
  linePrintRawAt(header, YELLOW + "MTVText", 11);
  let lastSub;
  for (let i = 0;; i++) for (const [idx, page] of Object.entries(content)) {
    if (idx === "888") {
      const sub = subs && getSubtitle(subs);
      if (sub !== lastSub) {
        lastSub = sub;
        pageErase(page);
        if (sub) {
          let pos = 24 - sub.lines.length*(doubleHeightSubs ? 2 : 1);
          const prefix = doubleHeightSubs ? DOUBLE_HEIGHT+START_BOX+START_BOX : START_BOX+START_BOX;
          const suffix = doubleHeightSubs ? END_BOX+END_BOX+' ' : END_BOX+END_BOX;
          for (const line of sub.lines) {
            const l = (prefix+line+suffix).substring(0, 40)
            pagePrintAt(page, l, (40 - l.length)>>1, pos);
            pos += doubleHeightSubs ? 2 : 1;
          }
        }
      }
    }
    const n = parseInt(idx, 16);
    if (((n>>8) & 7) !== page.magazine) console.log('magazine mismatch for page ', idx);
    header.writeUInt8(hamming84[page.magazine], 0);
    header.writeUInt8(hamming84[n&0x0f], 2);
    header.writeUInt8(hamming84[(n&0xf0)>>4], 3);
    // Subpage + C4-C6
    header.writeUInt8(hamming84[0], 4);
    header.writeUInt8(hamming84[page.clean?0:8], 5); // C4 - erase
    header.writeUInt8(hamming84[0], 6);
    header.writeUInt8(hamming84[page.subtitle?8:0], 7); // C6 - subtitle
    linePrintRawAt(header, `${i%10}`, 20);
    headerAddTime(header);
    await sendTeletext([header, getPageBuffer(page)]);
    console.log(i, n.toString(16));
  }
  childProcess.stdin.end();
}
function processInput(buf) {
  pagePrintAt(content[200], buf.substring(0,40), 0, 1);
  if (buf[0] === 'F') {
    const playerFile = buf.substring(1);
    const baseName = playerFile.replace(/\.\w+$/,"");
    try {
    const srtContent = readFileSync(baseName + '.srt');
    subs = srtContent ? parseSrt(srtContent.toString()) : undefined;
    } catch(e) { subs = undefined }
  } else if (buf[0] === 'P') {
    playerPos = parseInt(buf.substring(1));
  } else {
    console.log('Unknown player command: ', buf);
  }
}
main();

function generateBinaryPacket() {
  return Buffer.alloc(42, parity[32]); // Create a new buffer of specified length
}
function setPacketAddress(buf, line, x, y, dc = undefined) {
  const b1 = hamming84[x + (y&1 ? 8 : 0)];
  const b2 = hamming84[y >> 1];
  buf.writeUInt8(b1, 0 + line * 42);
  buf.writeUInt8(b2, 1 + line * 42);
  if (dc !== undefined) buf.writeUInt8(hamming84[dc], 2 + line * 42)
}

function getSubtitle(subs) {
  const time = playerPos;
  return subs.find(({startTimeMs, endTimeMs}) => time >= startTimeMs && time <= endTimeMs);
}
function parseSrt(subtitleText) {
  const subtitleBlocks = subtitleText.split('\n\n');
  const subtitles = [];
  for (const block of subtitleBlocks) {
    const lines = block.trim().split('\n');
    const id = parseInt(lines[0], 10);
    if (lines.length < 3) continue;
    const [startTimeMs, endTimeMs] = lines[1].split(' --> ').map(timeString => {
      const [hh, mm, ss, ms] = timeString.split(/[^0-9]/).map(parseFloat);
      return hh * 3600000 + mm * 60000 + ss * 1000 + ms;
    });
    subtitles.push({
      id,
      startTimeMs,
      endTimeMs,
      lines: lines.slice(2, 10).map(x => x.trim()),
    });
  }
  return subtitles;
}
