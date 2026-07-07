#!/usr/bin/env node
'use strict';
var fs   = require('fs');
var path = require('path');
var root = path.resolve(__dirname, '..');
var html = fs.readFileSync(path.join(root, 'src/pkjs/settings.html'), 'utf8');
var out  = '/* AUTO-GENERATED from src/pkjs/settings.html — edit that file, not this one */\n' +
           'module.exports = ' + JSON.stringify(html) + ';\n';
fs.writeFileSync(path.join(root, 'src/pkjs/settings-html.js'), out);
console.log('build-settings: ' + html.length + ' chars -> settings-html.js');
