const fetch = require('node-fetch');
const xml2js = require('xml2js');
const cheerio = require('cheerio'); // Import cheerio library
const { exit } = require('process');

// https://github.com/kazuhikoarase/qrcode-generator/blob/master/js/qrcode.js

const rssFeedURL = 'https://www.billboard.com/feed';
//const rssFeedURL = "https://www.nme.com/news/music/feed"
//const rssFeedURL = 'https://feeds.arstechnica.com/arstechnica/index/';
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
async function fetchAndParseRSSFeed() {
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
    const plainTextItems = items.map((item) => {
      // Use cheerio to parse HTML content and extract plain text
      const $ = cheerio.load(item['content:encoded'][0]);
      // Remove elements with the "injected-related-story" class
      $('.injected-related-story,.wp-block-embed,.post-content-read-more,ul>li:only-child>strong:only-child,.intro-image,pp:not(div#rss-wrap p)').remove();
      // if ($('.pmc-ecomm-disclaimer').length > 0) {
      //   // Skip this item
      //   return '';
      // }
      $('strong').each(function () {
        const strongText = $(this).text();
        $(this).text(`(${strongText})`);
      });
      $('em').each(function () {
        const strongText = $(this).text();
        $(this).text(`{${strongText}}`);
      });
      const plainTextContent = $.text();
      const cleanedText = plainTextContent.replace(/[\t ]{2,}/g, ' ').replace(/^[\t ]/mg, '').replace(/\n{2,}/g, '\n');

      return `Title ${item.title[0].length}: ${item.title[0]}\nCategory: ${item.category}\nGuid: ${item.guid[0]}\nDescription ${cleanedText.length}:\n${formatParagraphs(cleanedText)}\n\n`;
    });

    // Join the plain text items into a single string
    const plainTextFeed = plainTextItems.join('\n');

    console.log(plainTextFeed);
  } catch (error) {
    console.error('Error:', error);
  }
}

fetchAndParseRSSFeed();
