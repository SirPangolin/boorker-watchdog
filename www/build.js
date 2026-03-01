const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const zlib = require('zlib');

const SRC = path.join(__dirname, 'src');
const DIST = path.join(__dirname, 'dist');

// Clean dist
if (fs.existsSync(DIST)) {
    fs.rmSync(DIST, { recursive: true });
}
fs.mkdirSync(DIST, { recursive: true });
fs.mkdirSync(path.join(DIST, 'css'), { recursive: true });
fs.mkdirSync(path.join(DIST, 'js'), { recursive: true });

// Build JS with esbuild
console.log('Building JS...');
execSync(`npx esbuild ${path.join(SRC, 'js', 'app.js')} --bundle --minify --outfile=${path.join(DIST, 'js', 'app.js')}`, { stdio: 'inherit' });

// Minify CSS (simple - remove comments and whitespace)
console.log('Building CSS...');
let css = fs.readFileSync(path.join(SRC, 'css', 'app.css'), 'utf8');
css = css.replace(/\/\*[\s\S]*?\*\//g, '');  // Remove comments
css = css.replace(/\s+/g, ' ');              // Collapse whitespace
css = css.replace(/\s*([{}:;,])\s*/g, '$1'); // Remove space around punctuation
fs.writeFileSync(path.join(DIST, 'css', 'app.css'), css.trim());

// Copy HTML files
console.log('Copying HTML...');
fs.copyFileSync(path.join(SRC, 'index.html'), path.join(DIST, 'index.html'));
fs.copyFileSync(path.join(SRC, 'login.html'), path.join(DIST, 'login.html'));

// Gzip all files
console.log('Gzipping...');
function gzipFile(filepath) {
    const content = fs.readFileSync(filepath);
    const gzipped = zlib.gzipSync(content, { level: 9 });
    fs.writeFileSync(filepath + '.gz', gzipped);

    const originalSize = content.length;
    const gzippedSize = gzipped.length;
    const savings = ((1 - gzippedSize / originalSize) * 100).toFixed(1);
    console.log(`  ${path.basename(filepath)}: ${originalSize} -> ${gzippedSize} (${savings}% saved)`);
}

gzipFile(path.join(DIST, 'index.html'));
gzipFile(path.join(DIST, 'login.html'));
gzipFile(path.join(DIST, 'css', 'app.css'));
gzipFile(path.join(DIST, 'js', 'app.js'));

console.log('Build complete!');
