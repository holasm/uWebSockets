var fs = require('fs');

var data = fs.readFileSync('./../src/Asio.h', 'utf-8');
var lines = data.split("\n");

lines.map((line, index) => {
	// remove comments
	if (line.trim().indexOf('//') == 0) { return }
	console.log(line)
})