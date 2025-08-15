import 'dotenv/config';
import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';
import cors from 'cors';
import { createProxyMiddleware } from 'http-proxy-middleware';
import { WebSocketServer, WebSocket } from 'ws';
import http from 'http';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const app = express();
const server = http.createServer(app);

/* ========= ENV ========= */
const PORT  = process.env.PORT || 8080;

const HA_BASE_URL = process.env.HA_BASE_URL || 'http://homeassistant.local:8123';
const HA_TOKEN    = process.env.HA_TOKEN || '';

const CORS_ALLOWED = (process.env.CORS_ALLOWED_ORIGINS || '')
  .split(',').map(s => s.trim()).filter(Boolean);

/* Погода/настройки UI */
const LAT  = parseFloat(process.env.LATITUDE  || '50.4501');
const LON  = parseFloat(process.env.LONGITUDE || '30.5234');
const TZ   = process.env.TIMEZONE || 'Europe/Kyiv';
const TEMP_UNIT = process.env.TEMP_UNIT || 'celsius';
const WIND_UNIT = process.env.WIND_UNIT || 'ms';
const WEATHER_REFRESH_MS   = parseInt(process.env.WEATHER_REFRESH_MS   || '900000', 10);
const SLIDESHOW_INTERVAL_MS= parseInt(process.env.SLIDESHOW_INTERVAL_MS|| '15000', 10);
const PHOTOS_MANIFEST_URL  = process.env.PHOTOS_MANIFEST_URL || '/photos/photos.json';

const HA_TOGGLES_JSON = process.env.HA_TOGGLES_JSON || '[]';
const HA_SCENES_JSON  = process.env.HA_SCENES_JSON  || '[]';
const HA_SENSORS_JSON = process.env.HA_SENSORS_JSON || '[]';

/* ========= MIDDLEWARE ========= */
app.use(express.json({ limit: '1mb' }));
if (CORS_ALLOWED.length) {
  app.use(cors({ origin: CORS_ALLOWED, credentials: false }));
}

/* ========= STATIC ========= */
app.use(express.static(path.join(__dirname, '..', 'public')));

/* ========= RUNTIME CONFIG ========= */
app.get('/config.json', (_req, res) => {
  res.json({
    latitude: LAT,
    longitude: LON,
    timezone: TZ,
    units: { temperature: TEMP_UNIT, wind: WIND_UNIT },
    weatherRefreshMs: WEATHER_REFRESH_MS,
    slideshowIntervalMs: SLIDESHOW_INTERVAL_MS,
    photosManifestUrl: PHOTOS_MANIFEST_URL,
    ha: {
      // фронт не знает URL/токен HA — он ходит в этот сервер
      entities: {
        toggles: JSON.parse(HA_TOGGLES_JSON),
        scenes:  JSON.parse(HA_SCENES_JSON),
        sensors: JSON.parse(HA_SENSORS_JSON)
      }
    }
  });
});

/* ========= REST → HA proxy =========
   Клиент вызывает: POST /ha/services/:domain/:service
   Мы форвардим в:  /api/services/:domain/:service   c Authorization: Bearer <TOKEN>
*/
app.use('/ha/services', createProxyMiddleware({
  target: HA_BASE_URL,
  changeOrigin: true,
  pathRewrite: { '^/ha/services': '/api/services' },
  headers: { 'Authorization': `Bearer ${HA_TOKEN}` }
}));

/* (Опционально) GET /ha/states -> /api/states для отладки */
app.use('/ha/states', createProxyMiddleware({
  target: HA_BASE_URL,
  changeOrigin: true,
  pathRewrite: { '^/ha/states': '/api/states' },
  headers: { 'Authorization': `Bearer ${HA_TOKEN}` }
}));

/* ========= SPA fallback ========= */
app.get('*', (req, res, next) => {
  if (req.path.startsWith('/ha/')) return next();
  res.sendFile(path.join(__dirname, '..', 'public', 'index.html'));
});

/* ========= WS tunnel =========
   Клиент подключается к ws(s)://<SERVER>/ws
   Сервер открывает ws к HA /api/websocket и сам шлёт auth с токеном
*/
const wss = new WebSocketServer({ server, path: '/ws' });

wss.on('connection', (client) => {
  const wsUrl = HA_BASE_URL.replace(/^http/i, 'ws') + '/api/websocket';
  const upstream = new WebSocket(wsUrl);

  let authed = false;

  upstream.on('message', (buf) => {
    let msg;
    try { msg = JSON.parse(buf.toString()); } catch { /* pass */ }

    // Перехватываем handshake Home Assistant
    if (msg && msg.type === 'auth_required') {
      upstream.send(JSON.stringify({ type: 'auth', access_token: HA_TOKEN }));
      return;
    }
    if (msg && msg.type === 'auth_ok') {
      authed = true;
    }

    // Ретранслируем в браузер
    if (client.readyState === WebSocket.OPEN) {
      client.send(buf);
    }
  });

  upstream.on('open', () => { /* ждём auth_required */ });
  upstream.on('close', () => client.close());
  upstream.on('error', () => client.close());

  client.on('message', (buf) => {
    // Всё после auth_ok пробрасываем в HA
    if (authed && upstream.readyState === WebSocket.OPEN) {
      upstream.send(buf);
    }
  });
  client.on('close', () => { try { upstream.close(); } catch {} });
});

/* ========= START ========= */
server.listen(PORT, () => {
  console.log(`Dashboard server: http://0.0.0.0:${PORT}`);
});
