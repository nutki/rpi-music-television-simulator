const fetch = require('node-fetch');
const cheerio = require('cheerio');

const chartParams = {
  uktop40: {
    url: 'https://www.officialcharts.com/charts/uk-top-40-singles-chart/',
    title: 'UK Top 40 Singles Chart',
    chartListSelector: '.chart-item-content',
    removeElements: '.movement-icon,.sr-only',
    positionSelector: '.chart-key',
    titleSelector: '.chart-name',
    artistSelector: '.chart-artist',
    lastWeekSelector: '.movement span.font-bold',
    dateSelector: 'h1 + p'
  },
  billboard100: {
    url: 'https://www.billboard.com/charts/hot-100/',
    title: 'Billboard Hot 100',
    chartListSelector: '.o-chart-results-list-row',
    positionSelector: 'ul.o-chart-results-list-row>li:nth-child(1) .c-label:nth-child(1)',
    titleSelector: 'li:nth-child(4) li:nth-child(1) .c-title',
    artistSelector: 'li:nth-child(4) li:nth-child(1) .c-label',
    lastWeekSelector: 'li:nth-child(4)>ul>li:nth-child(4) .c-label',
    dateSelector: '.a-heading-border + p.c-tagline',
  },
}

function fetchChart(chartName) {
  const params = chartParams[chartName];
  return fetch(params.url)
    .then((response) => {
      if (!response.ok) {
        throw new Error('Failed to fetch the webpage');
      }
      return response.text();
    })
    .then((body) => {
      const $ = cheerio.load(body);
      const chartItems = $(params.chartListSelector);
      const date = $(params.dateSelector).text().trim();
      let month, day, year;
      for (const part of date.split(/[,/ -]+/)) {
        if (!month && /^(jan(uary)?|feb(ruary)?|mar(ch)?|apr(il)?|may|june?|july?|aug(ust)?|sep(tember)?|oct(ober)?|nov(ember)?|dec(ember)?)$/i.test(part)) {
          month = ("janfebmaraprmayjunjulaugsepoctnovdec".indexOf(part.substring(0,3).toLowerCase()) / 3 + 1).toString().padStart(2, '0');
        }
        if (!day && /^\d+(st|nd|rd|th)?$/i.test(part) && parseInt(part) >= 1 && parseInt(part) <= 31) day = part.padStart(2, '0');
        if (!year && /^2\d\d\d$/.test(part)) year = part;
      }
      const parsedDate = day && month && year ? [day,month,year].join('/') : undefined;
      const result = [];
      chartItems.each((index, element) => {
        if (params.removeElements) $(element).find(params.removeElements).remove();
        const position = $(element).find(params.positionSelector).text().trim();
        const title = $(element).find(params.titleSelector).text().trim();
        const artist = $(element).find(params.artistSelector).text().trim();
        const lastWeek = $(element).find(params.lastWeekSelector).text().trim();
        result.push({
          position, title, artist, lastWeek,
        });
      });
      return { data: result, title: params.title, date, parsedDate };
    })
    .catch((error) => {
      console.error(error.message);
    });
    return [];
}
module.exports = {
  fetchChart
}
