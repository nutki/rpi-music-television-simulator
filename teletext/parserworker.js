const { parentPort } = require('worker_threads');
const { fetchAndParseRSSFeed } = require('./rss');
const { fetchChart } = require('./charts');

parentPort.on('message', (message) => {
  console.log('work work work', message);
  if (message.method === 'rss') {
    fetchAndParseRSSFeed(...message.params).then(result => {
      parentPort.postMessage({id: message.id, result});
    });
  } else if (message.method === 'chart') {
    fetchChart(...message.params).then(result => {
      parentPort.postMessage({id: message.id, result});
    });
  }
});
