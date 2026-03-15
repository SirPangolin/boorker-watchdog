import { defineConfig } from 'vite';
import { readFileSync, existsSync } from 'fs';
import { resolve } from 'path';

export default defineConfig(({ mode }) => {
  const useMock = process.env.VITE_MOCK === 'true' || mode === 'development';
  const deviceIp = process.env.VITE_DEVICE_IP;

  return {
    root: 'src',
    build: {
      outDir: '../dist',
      emptyOutDir: true,
    },
    server: {
      proxy: deviceIp ? {
        '/api': {
          target: `http://${deviceIp}`,
          changeOrigin: true,
        },
      } : undefined,
    },
    plugins: useMock && !deviceIp ? [mockApiPlugin()] : [],
  };
});

function mockApiPlugin() {
  return {
    name: 'mock-api',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (!req.url.startsWith('/api/')) return next();

        const urlPath = req.url.split('?')[0];
        const mockPath = resolve(import.meta.dirname, 'mock', urlPath.slice(1) + '.json');

        if (req.method === 'GET' && existsSync(mockPath)) {
          const data = readFileSync(mockPath, 'utf-8');
          res.setHeader('Content-Type', 'application/json');
          res.end(data);
          return;
        }

        // POST /api/v1/auth/login — mock login with password_change_required
        if (req.method === 'POST' && urlPath === '/api/v1/auth/login') {
          let body = '';
          req.on('data', c => body += c);
          req.on('end', () => {
            let parsed;
            try { parsed = JSON.parse(body); } catch {
              res.statusCode = 400;
              res.setHeader('Content-Type', 'application/json');
              res.end(JSON.stringify({ error: true, message: 'Invalid request body' }));
              return;
            }
            const { password } = parsed;
            res.setHeader('Content-Type', 'application/json');
            if (password === 'admin' || password === 'newpassword') {
              const firstBoot = password === 'admin';
              res.setHeader('Set-Cookie', 'session=mock-session-token; Path=/; HttpOnly');
              res.end(JSON.stringify({
                success: true,
                password_change_required: firstBoot,
              }));
            } else {
              res.statusCode = 401;
              res.end(JSON.stringify({ error: true, attempts_remaining: 4 }));
            }
          });
          return;
        }

        if (req.method === 'POST' && urlPath === '/api/v1/auth/logout') {
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ success: true }));
          return;
        }

        if (req.method === 'PUT' && urlPath === '/api/v1/auth/password') {
          let body = '';
          req.on('data', c => body += c);
          req.on('end', () => {
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ success: true }));
          });
          return;
        }

        if (req.method === 'POST' && urlPath === '/api/v1/system/reboot') {
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ success: true, delay: 2 }));
          return;
        }

        if (req.method === 'POST' && urlPath === '/api/v1/system/factory-reset') {
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ success: true, message: 'Factory reset complete, rebooting...' }));
          return;
        }

        if (urlPath.startsWith('/api/v1/ota')) {
          if (req.method === 'PUT') {
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ success: true }));
            return;
          }
          if (req.method === 'DELETE') {
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ success: true }));
            return;
          }
          if (req.method === 'GET') {
            const otaMock = resolve(import.meta.dirname, 'mock/api/v1/ota/status.json');
            if (existsSync(otaMock)) {
              res.setHeader('Content-Type', 'application/json');
              res.end(readFileSync(otaMock, 'utf-8'));
              return;
            }
          }
        }

        // Fallback: 404
        res.statusCode = 404;
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({ error: true, message: 'Mock not found: ' + urlPath }));
      });
    },
  };
}
