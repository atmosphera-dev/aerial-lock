// Build-time artwork tool, run only in the project VM. No app dependency on SVG.
const {chromium} = require(process.env.PLAYWRIGHT_MODULE);
const fs = require('node:fs');
const path = require('node:path');
(async () => {
  const output = process.argv[2];
  if (!output) throw new Error('usage: render-icons.cjs OUTPUT_DIRECTORY');
  fs.mkdirSync(output, {recursive: true});
  const svg = fs.readFileSync(path.join(__dirname, 'org.aeriallock.AerialSettings.svg'), 'utf8');
  const browser = await chromium.launch({headless: true});
  try {
    for (const size of [16, 24, 32, 48, 64, 128, 256]) {
      const page = await browser.newPage({viewport: {width: size, height: size}, deviceScaleFactor: 1});
      await page.setContent('<style>html,body{margin:0;width:100%;height:100%;background:transparent}svg{width:100%;height:100%}</style>' + svg);
      await page.screenshot({path: path.join(output, size + '.png'), omitBackground: true});
      await page.close();
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
