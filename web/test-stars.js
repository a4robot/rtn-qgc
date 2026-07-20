const fs = require('fs');
const code = fs.readFileSync('src/components/map/CoreMap.tsx', 'utf8');
console.log(code.includes('alpha'));
