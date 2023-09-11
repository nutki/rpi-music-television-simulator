const fetch = require('node-fetch');
const cheerio = require('cheerio');

const chartParams = {
  uktop40: {
    url: 'https://www.officialcharts.com/charts/uk-top-40-singles-chart/',
    chartListSelector: '.chart-item-content',
    removeElements: '.movement-icon,.sr-only',
    positionSelector: '.chart-key',
    titleSelector: '.chart-name',
    artistSelector: '.chart-artist',
    lastWeekSelector: '.movement span.font-bold',
  },
  billboard100: {
    url: 'https://www.billboard.com/charts/hot-100/',
    chartListSelector: '.o-chart-results-list-row',
    positionSelector: 'ul.o-chart-results-list-row>li:nth-child(1) .c-label:nth-child(1)',
    titleSelector: 'li:nth-child(4) li:nth-child(1) .c-title',
    artistSelector: 'li:nth-child(4) li:nth-child(1) .c-label',
    lastWeekSelector: 'li:nth-child(4)>ul>li:nth-child(4) .c-label',
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
      return result;
    })
    .catch((error) => {
      console.error(error.message);
    });
    return [];
}
module.exports = {
  fetchChart
}
