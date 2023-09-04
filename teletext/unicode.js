const discriticsStripMap = [
  ['A', 'ÀÁÂẦẤẪẨÃĀĂẰẮẴẲȦǠÄǞẢÅǺǍȀȂẠẬẶḀĄÆǼǢ'],
  ['a', 'ẚàáâầấẫẩãāăằắẵẳȧǡäǟảåǻǎȁȃạậặḁąæǽǣ'],
  ['B', 'ḂḄḆɃƁ'],
  ['b', 'ḃḅḇƀɓ'],
  ['C', 'ĆĈĊČÇḈƇȻ'],
  ['c', 'ćĉċčçḉƈȼ'],
  ['D', 'ḊĎḌḐḒḎĐƋƊƉÐ'],
  ['d', 'ḋďḍḑḓḏđƌɖɗ'],
  ['E', 'ÈÉÊỀẾỄỂẼĒḔḖĔĖËẺĚȄȆẸỆȨḜĘḘḚ'],
  ['e', 'èéêềếễểẽēḕḗĕėëẻěȅȇẹệȩḝęḙḛɇ'],
  ['F', 'ḞƑ'],
  ['f', 'ḟƒ'],
  ['G', 'ǴĜḠĞĠǦĢǤƓꞠ'],
  ['g', 'ǵĝḡğġǧģǥɠꞡ'],
  ['H', 'ĤḢḦȞḤḨḪĦⱧ'],
  ['h', 'ĥḣḧȟḥḩḫẖħⱨ'],
  ['I', 'ÌÍÎĨĪĬİÏḮỈǏȈȊỊĮḬƗ'],
  ['i', 'ìíîĩīĭïḯỉǐȉȋịįḭɨı'],
  ['J', 'ĴɈ'],
  ['j', 'ĵǰɉ'],
  ['K', 'ḰǨḲĶḴƘⱩꝀꝂꝄꞢ'],
  ['k', 'ḱǩḳķḵƙⱪꝁꝃꝅꞣĸ'],
  ['L', 'ĿĹĽḶḸĻḼḺŁȽⱢⱠꝈꝆ'],
  ['l', 'ŀĺľḷḹļḽḻſłƚɫⱡꝉꞁꝇ'],
  ['M', 'ḾṀṂⱮƜ'],
  ['m', 'ḿṁṃɱɯ'],
  ['N', 'ǸŃÑṄŇṆŅṊṈȠƝꞐꞤŊ'],
  ['n', 'ǹńñṅňṇņṋṉƞɲŉꞑꞥŋ'],
  ['O', 'ÒÓÔỒỐỖỔÕṌȬṎŌṐṒŎȮȰÖȪỎŐǑȌȎƠỜỚỠỞỢỌỘǪǬØǾŒ'],
  ['o', 'òóôồốỗổõṍȭṏōṑṓŏȯȱöȫỏőǒȍȏơờớỡởợọộǫǭøǿœ'],
  ['P', 'ṔṖƤⱣꝐꝒ'],
  ['p', 'ṕṗƥᵽꝑꝓ'],
  ['Q', 'ꝖꝘɊ'],
  ['q', 'ɋꝗꝙ'],
  ['R', 'ŔṘŘȐȒṚṜŖṞɌⱤꞦ'],
  ['r', 'ŕṙřȑȓṛṝŗṟɍɽꞧ'],
  ['S', 'ẞŚṤŜṠŠṦṢṨȘŞⱾꞨ'],
  ['s', 'ßśṥŝṡšṧṣṩșşȿꞩ'],
  ['T', 'ṪŤṬȚŢṰṮŦƬƮȾ'],
  ['t', 'ṫẗťṭțţṱṯŧƭʈⱦ'],
  ['U', 'ÙÚÛŨṸŪṺŬÜǛǗǕǙỦŮŰǓȔȖƯỪỨỮỬỰỤṲŲṶṴɄ'],
  ['u', 'ùúûũṹūṻŭüǜǘǖǚủůűǔȕȗưừứữửựụṳųṷṵʉµ'],
  ['V', 'ṼṾꝞ'],
  ['v', 'ṽṿꝟ'],
  ['W', 'ẀẂŴẆẄẈⱲ'],
  ['w', 'ẁẃŵẇẅẘẉⱳ'],
  ['X', 'ẊẌ'],
  ['x', 'ẋẍ'],
  ['Y', 'ỲÝŶỸȲẎŸỶỴƳɎĲ'],
  ['y', 'ỳýŷỹȳẏÿỷẙỵƴɏĳ'],
  ['Z', 'ŹẐŻŽẒẔƵȤⱿⱫ'],
  ['z', 'źẑżžẓẕƶȥɀⱬ'],
];
const chr = (x) => x.codePointAt(0);
const g0dia = (x, d) => (0x10 + d) + (x << 5);
const g0 = (x) => 0x10 + (x << 5);
const g2 = (x) => 0x0f + (x << 5);
const charMap = {
  // G0 Latin English
 '£': chr('#'),
 '←': chr('['),
 '½': chr('\\'),
 '→': chr(']'),
 '↑': chr('^'),
 '#': chr('_'), // < 128, check if does not break graphics
 '─': chr('`'),
 '¼': chr('{'),
 '‖': chr('|'),
 '¾': chr('}'),
 '÷': chr('~'),
};
const x26CharMap = {
  // G0 ASCII Characters shadowed by national set
  '#': g0(chr('#')),
  '[': g0(chr('[')),
  '\\': g0(chr('\\')),
  ']': g0(chr(']')),
  '^': g0(chr('^')),
  '_': g0(chr('_')),
  '`': g0(chr('`')),
  '{': g0(chr('{')),
  '|': g0(chr('|')),
  '}': g0(chr('}')),
  '~': g0(chr('~')),
  // G2 Latin
  '¡': g2(chr('!')),
  '¢': g2(0x22),
  '£': g2(0x23), // Also in latin english G0
  '$': g2(0x24), // Also in latin english G0
  '¥': g2(0x25),
  '#': g2(0x26), // Also in latin english G0
  '§': g2(0x27),
  '¤': g2(0x28),
  '‘': g2(0x29),
  '“': g2(0x2A),
  '«': g2(0x2B),
  '←': g2(0x2C), // Also in latin english G0
  '↑': g2(0x2D), // Also in latin english G0
  '→': g2(0x2E), // Also in latin english G0
  '↓': g2(0x2F),

  '°': g2(0x30),
  '±': g2(0x31),
  '²': g2(0x32),
  '³': g2(0x33),
  '×': g2(0x34),
  'µ': g2(0x35),
  '¶': g2(0x36),
  '·': g2(0x37),
  '÷': g2(0x38), // Also in latin english G0
  '’': g2(0x39),
  '”': g2(0x3A),
  '»': g2(0x3B),
  '¼': g2(0x3C), // Also in latin english G0
  '½': g2(0x3D), // Also in latin english G0
  '¾': g2(0x3E), // Also in latin english G0
  '¿': g2(0x3F),

  '─': g2(0x50), // Also in latin english G0
  '¹': g2(0x51),
  '®': g2(0x52),
  '©': g2(0x53),
  '™': g2(0x54),
  '♪': g2(0x55),
  '₠': g2(0x56),
  '‰': g2(0x57),
  'α': g2(0x58),
  '⅛': g2(0x5C),
  '⅜': g2(0x5D),
  '⅝': g2(0x5E),
  '⅞': g2(0x5F),

  'Ω': g2(0x60),
  'Æ': g2(0x61),
  'Ð': g2(0x62),
  'ª': g2(0x63),
  'Ħ': g2(0x64),
  'Ĳ': g2(0x66),
  'Ŀ': g2(0x67),
  'Ł': g2(0x68),
  'Ø': g2(0x69),
  'Œ': g2(0x6A),
  'º': g2(0x6B),
  'Þ': g2(0x6C),
  'Ŧ': g2(0x6D),
  'Ŋ': g2(0x6E),
  'ŉ': g2(0x6F),

  'ĸ': g2(0x70),
  'æ': g2(0x71),
  'đ': g2(0x72),
  'ð': g2(0x73),
  'ħ': g2(0x74),
  'ı': g2(0x75),
  'ĳ': g2(0x76),
  'ŀ': g2(0x77),
  'ł': g2(0x78),
  'ø': g2(0x79),
  'œ': g2(0x7A),
  'ß': g2(0x7B),
  'þ': g2(0x7C),
  'ŧ': g2(0x7D),
  'ŋ': g2(0x7E),
};
// G2 diacritics (0x41 - 0x4F) to unicode composable diacritic codes
var diacritcs = [0,0x300,0x301,0x302,0x303,0x304,0x306,0x307,0x308,0x323,0x30A,0x327,0x332,0x30B,0x328,0x30C];
for (const [s, codes] of discriticsStripMap) {
  const mapTo = chr(s);
  for (const code of codes) {
    charMap[code] = mapTo;
    const x = code.normalize("NFD");
    if (x.length != 2) continue;
    const [ch, dia] = [...x].map(x => x.codePointAt(0));
    const diaMapped = diacritcs.indexOf(dia);
    if (diaMapped > 0 && (ch >= chr('A') && ch <= chr('Z') || ch >= chr('a') && ch <= chr('z'))) {
      x26CharMap[code] = g0dia(ch, diaMapped);
    }
  }
}
module.exports = {
    charMap, x26CharMap
}
