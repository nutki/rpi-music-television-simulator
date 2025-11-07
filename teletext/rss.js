const fetch = require('node-fetch').default;
const xml2js = require('xml2js');
const cheerio = require('cheerio'); // Import cheerio library
const { exit } = require('process');

// https://github.com/kazuhikoarase/qrcode-generator/blob/master/js/qrcode.js

const params = {
Billboard: {
  rssFeedURL: 'https://www.billboard.com/feed',
  includeCategories: ['Music', 'Music News', 'Awards'],
  excludeCategories: ['Video', 'Product Recommendations', 'Elon Musk'],
  filterContent: '.injected-related-story,.wp-block-embed',
},
NME: {
  rssFeedURL: "https://www.nme.com/news/music/feed",
  includeCategories: ['Music', 'Music News', 'Awards'],
  excludeCategories: ['Elon Musk'],
  filterContent: '.twitter-tweet,.tiktok-embed,.post-content-read-more,ul>li:only-child>strong:only-child',
},
Ars: {
  rssFeedURL: 'https://feeds.arstechnica.com/arstechnica/index/',
  excludeCategories: ['Elon Musk'],
},
};

function formatParagraphs(paragraphs) {
  const maxLineLength = 40;
  let formattedText = "";

  for (const paragraph of paragraphs.split('\n')) {
    const words = paragraph.split(' ');
    let currentLine = ' ';

    for (const word of words) {
      if (currentLine.length + word.length > maxLineLength) {
        // If adding the word exceeds the line length, start a new line
        formattedText += currentLine.trimEnd() + '\n';
        currentLine = '';
      }
      currentLine += word + ' ';
    }

    // Add the current line of the paragraph to the formatted text
    formattedText += currentLine.trimEnd() + '\n';
  }

  return formattedText;
}


// Function to fetch and parse the RSS feed
async function fetchAndParseRSSFeed(feedName) {
  const { rssFeedURL, includeCategories, excludeCategories, filterContent }  = params[feedName || 'Billboard'];
  try {
    const response = await fetch(rssFeedURL);
    if (!response.ok) {
      throw new Error('Failed to fetch RSS feed');
    }

    const rssData = await response.text();

    // Parse the XML data into a JavaScript object
    const parser = new xml2js.Parser({ explicitArray: true, ignoreAttrs: true });
    const parsedData = await parser.parseStringPromise(rssData);

    // Extract and format plain text from the parsed data
    const items = parsedData.rss.channel[0].item;
    const result = [];
    items.forEach((item) => {
      if (includeCategories && !item.category.some(c => includeCategories.includes(c))) return;
      if (excludeCategories && item.category.some(c => excludeCategories.includes(c))) return;
      const $ = cheerio.load(item['content:encoded'][0]);
      if (filterContent) $(filterContent).remove();
      $('strong').each(function () {
        const strongText = $(this).text();
        $(this).text('\x03' + strongText.trim().replace(/[\u00A0 ]/g, '\x03') + '\x07');
      });
      $('em').each(function () {
        const emText = $(this).text();
        $(this).text('\x06' + emText.trim().replace(/[\u00A0 ]/g, '\x06') + '\x07');
      });
      $('a').each(function () {
        const anchorText = $(this).text();
        $(this).text('\x02' + anchorText.trim().replace(/[\u00A0 ]/g, '\x02') + '\x07');
      });
      const plainTextContent = $.text();
      let cleanedText = plainTextContent.replace(/[\u00A0\t]/g, ' ').replace(/ {2,}/g, ' ').replace(/^ /mg, '').replace(/\n{2,}/g, '\n');
      cleanedText = cleanedText.replace(/([\0-\7])([?!,.”’]|['’]s)/g, (_,a,b) => b+a);
      cleanedText = cleanedText.replace(/([“‘])([\0-\7])/g, (_,a,b) => b+a);
      cleanedText = cleanedText.replace(/ ?[\0-\7] ?/g, x => x.trim());
      cleanedText = cleanedText.replace(/[\0-\7]+([\0-\7]|\n|$)/mg, (_,v) => v);
      result.push({
        title: item.title[0],
        url: item.guid[0]?.startsWith('http') ? item.guid[0] : item.link[0],
        text: cleanedText,
      });
    });
    return result;
  } catch (error) {
    console.error('Error:', error);
  }
  return [];
}

module.exports = {
  fetchAndParseRSSFeed,
}
