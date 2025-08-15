import 'dotenv/config';
import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';
import cors from 'cors';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 8080;

/* ===== ENV / CONFIG ===== */
const CORS_ALLOWED = (process.env.CORS_ALLOWED_ORIGINS || '')
  .split(',').map(s => s.trim()).filter(Boolean);

const LAT  = parseFloat(process.env.LATITUDE  || '50.4501');
const LON  = parseFloat(process.env.LONGITUDE || '30.5234');
const TZ   = process.env.TIMEZONE || 'Europe/Kyiv';
const TEMP_UNIT = process.env.TEMP_UNIT || 'celsius';
const WIND_UNIT = process.env.WIND_UNIT || 'ms';
const WEATHER_REFRESH_MS    = parseInt(process.env.WEATHER_REFRESH_MS    || '900000', 10);
const SLIDESHOW_INTERVAL_MS = parseInt(process.env.SLIDESHOW_INTERVAL_MS || '15000', 10);

const PHOTOS_MANIFEST_URL   = process.env.PHOTOS_MANIFEST_URL || '/photos/photos.json';
let   PHOTOS_FALLBACK = [];
try { PHOTOS_FALLBACK = JSON.parse(process.env.PHOTOS_FALLBACK_JSON || '[]'); } catch {}

/* ===== MIDDLEWARE ===== */
app.use(express.json({ limit: '1mb' }));
if (CORS_ALLOWED.length) app.use(cors({ origin: CORS_ALLOWED, credentials: false }));

/* ===== STATIC ===== */
app.use(express.static(path.join(__dirname, '..', 'public')));

/* ===== RUNTIME CONFIG for SPA ===== */
app.get('/config.json', (_req, res) => {
  res.json({
    latitude: LAT,
    longitude: LON,
    timezone: TZ,
    units: { temperature: TEMP_UNIT, wind: WIND_UNIT },
    weatherRefreshMs: WEATHER_REFRESH_MS,
    slideshowIntervalMs: SLIDESHOW_INTERVAL_MS,
    photosManifestUrl: PHOTOS_MANIFEST_URL,
    photosFallback: PHOTOS_FALLBACK
  });
});

/* ===== SPA fallback ===== */
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, '..', 'public', 'index.html'));
});

app.listen(PORT, () => {
  console.log(`Dashboard server: http://0.0.0.0:${PORT}`);
});
