var path = require('path');

module.exports = {
  mode: 'development',
  entry: "./src/index.jsx",
  module: {
    rules: [
      {
        test: /\.(js|jsx)$/,
        exclude: /node_modules/,
        use: {
          loader: "babel-loader"
        }
      }
    ]
  },
  devServer: {
    proxy: {
      '/api': 'http://192.168.1.32:3000',
      '/videos': 'http://192.168.1.32:3000',
    },
    contentBase: path.join(__dirname, 'dist'),
    compress: true,
    port: 9000
  },
};
