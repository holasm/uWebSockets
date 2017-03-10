var fs = require('fs');

var files = fs.readdirSync('./src');
var count = 0;
files.map((file) => {
	var data = fs.readFileSync('./src/'+ file, 'utf-8');
	count += data.split('\n').length;
})

console.log(count);