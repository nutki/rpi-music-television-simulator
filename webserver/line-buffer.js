exports.LineBuffer = class {
  _buffer = '';

  feed(chunk) {
    this._buffer += chunk
    if (chunk.includes('\n')) {
      const lines = this._buffer.split('\n');
      this._buffer = lines.pop();
      return lines;
    }
    return [];
  }
}
