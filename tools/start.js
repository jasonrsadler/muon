var path = require('path')
var execute = require('./lib/execute')

var env = {
  NODE_ENV: 'development'
}

execute('electron "' + path.join(__dirname, '..') + '" ' + process.argv[2], env)
