const fetch = require('node-fetch');
const wtf = require("wtf_wikipedia");
const fs = require('fs');

function editDistance (s1, s2) {
  s1 = s1.toLowerCase();
  s2 = s2.toLowerCase();
  let costs = new Array();
  for (let i = 0; i <= s1.length; i++) {
    let lastValue = i;
    for (let j = 0; j <= s2.length; j++) {
      if (i == 0) {
        costs[j] = j;
      } else {
        if (j > 0) {
          let newValue = costs[j - 1];
          if (s1.charAt(i - 1) != s2.charAt(j - 1)) {
            newValue = Math.min(Math.min(newValue + 2, lastValue + 1), costs[j] + 1);
          }
          costs[j - 1] = lastValue;
          lastValue = newValue;
        }
      }
    }
    if (i > 0) costs[s2.length] = lastValue;
  }
  return costs[s2.length];
}

function itemsToArray(s) {
  return (s.startsWith('* ') ? s.substring(2).split("\n* ") :
  s.startsWith('*') ? s.substring(1).split("\n*") :
  s.includes(' · ') ? s.split(" · ") :
  s.includes('\n\n') ? s.split("\n\n") :
  s.includes('\n') ? s.split("\n") :
  s.includes(', ') ? s.split(", ") :
  s ? [ s ] : []).map(e => e.trim());
}

const userAgent = 'MusicMetaSearch/0.0 (https://github.com/nutki) node-fetch wtf_wikipedia';

function scrapDirector(page) {
  if (!page) return "";
  const text = page.text();
  if (!text) return "";
  const m = text.match(/([Dd]irected by|[Dd]irector),? (\p{Lu}\p{L}*( (van|de|der|\p{Lu}\.|\p{Lu}\p{L}*)){1,3})/u);
  if (m && m[2]) return m[2];
  const m2 = text.match(/[Pp]hotographer,? (\p{Lu}\p{L}*( (van|de|der|\p{Lu}\.|\p{Lu}\p{L}*)){1,3})/u);
  if (m2 && m2[1]) return m2[1];
  const m3 = text.match(/[Ff]ilmmaker,? (\p{Lu}\p{L}*( (van|de|der|\p{Lu}\.|\p{Lu}\p{L}*)){1,3})/u);
  if (m3 && m3[1]) return m3[1];
  return "";
}

function scrap(query, page, resolve, reject) {
  console.log(`${query} => ${page}`);
  wtf.fetch(page, { userAgent }).then(d => {
    const mv_section = d.sections("Music Video");
    const director = scrapDirector(mv_section) || scrapDirector(d); // text of sections does not contain subsections
    const infos = d.infoboxes().filter(ib => ib.type() === "single" || ib.type() === "song" || ib.type() === "album").map(ib => {
        console.log(ib.data);
      const g = c => ib.data[c] && ib.data[c].text();
      let artist = g("artist").replace(/\bfeaturing\b/ig, "ft.") || "";
      if (artist.startsWith('the ')) artist = 'The ' + artist.substring(4);
      const name = ib.type() === "album" ? "" : (g("name") || "");
      const [ year ] = /\d{4}/.exec(g("released")) || [];
      const album = ib.type() === "album" ? g("name") || "" : g("album") || g('from album') || "";
      const label = (itemsToArray(g("label") || "")[0] || "").replace(/ *\([^(]+\).*/, "");
      const genres = itemsToArray(g("genre") || "").map(g => g.toLowerCase());
      const data = { artist, name, year, album, label, genres, director };
      const distance = Math.min(editDistance(`${artist} ${name}`, query), editDistance(`${name} ${artist}`, query));
      console.log("Label:", g("label"));
      return { distance, data };
    });
    infos.sort((a, b) => a.distance - b.distance);
    if (!infos.length) return reject(`No infobox found on ${page}`);
    return resolve(infos[0].data);
  });
}

const encodeGetParams = p => 
  Object.entries(p).map(kv => kv.map(encodeURIComponent).join("=")).join("&");

function queryMeta(query) {
  const params = {
    origin: "*",
    action: "query",
    list: "search",
    srsearch: query + ' ' + 'hastemplate:"Infobox song"',
    format: "json"
  };
  var url = "https://en.wikipedia.org/w/api.php?" + encodeGetParams(params);
  console.log(url)
  return new Promise((resolve, reject) => {
    fetch(url, { headers: { 'user-agent': userAgent }}).then(response => {
      if (response.status !== 200) {
        throw response;
      }
      return response.json().catch(e => {
        console.log("!");
        reject(e);
      });
    }).then(response => {
      if (!response.query.search.length) reject("No results");
      scrap(query, response.query.search[0].title, resolve, reject);
    }).catch(e => {
      console.log(e);
    });
  });
}

function cleanId(id) {
  return id
    .replace(/\((([^)]* (video|version))|19\d\d|20\d\d|720p?|1080p?|video oficial|official)\)/ig, "")
    .replace(/\[(([^\]]* (video|version))|19\d\d|20\d\d|720p?|1080p?|video oficial|official)\]/ig, "")
    .replace(/ +( |$)/g, " ")
    .replace(/\b(19|20)..$/, "");
}

async function getMetaForFilename(path, file) {
  const [, name] = file.match(/(.*)\.(mkv|mp4|mov)$/) || [];
  if (name) {
    const metaFileName = path + '/' + name + '.meta.json';
    const query = cleanId(name);
    if (!fs.existsSync(metaFileName)) try {
      const meta = await queryMeta(query);
      console.log(meta);
      fs.writeFileSync(metaFileName, JSON.stringify(meta, null, 2));
    } catch (e) {
      console.log(e);
    }
  }
}

function splitArtist(id) {
  if (id.includes(" - ")) {
    return id.split(" - ");
  } else if (id.includes("-")) {
    return id.split("-");
  } else if (id.includes(" by ")) {
    return id.split(" by ").reverse();
  }
  return [ "", id ];
}

function inferMetaFromId(id) {
  const [, year] = id.match(/\b(19..|20..)(\)|$)/) || [];
  const [artist, name] = splitArtist(cleanId(id));
  return {
    name: name || "",
    artist: artist || "",
    year: year || "",
    album: "",
    genres: [],
    label: "",
    director: "",
  }
}

module.exports = { queryMeta, getMetaForFilename, inferMetaFromId };
