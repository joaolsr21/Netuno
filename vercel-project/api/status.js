/**
 * /api/status
 *
 * GET  → retorna o último estado recebido do ESP32
 * POST → recebe dados do ESP32 (fallback HTTP quando MQTT não disponível)
 *
 * O ESP32 pode chamar: POST https://seu-projeto.vercel.app/api/status
 * com o mesmo JSON que publica no MQTT.
 *
 * Vercel usa serverless functions — não há estado em memória entre chamadas.
 * Para persistir, usamos Vercel KV (Redis) gratuito até 256MB.
 *
 * Setup Vercel KV:
 *   1. No dashboard Vercel → Storage → Create KV Database
 *   2. Em "Settings → Environment Variables" as variáveis são
 *      adicionadas automaticamente: KV_URL, KV_REST_API_URL, KV_REST_API_TOKEN
 */

const { kv } = require('@vercel/kv');   // npm install @vercel/kv

const KV_KEY     = 'aquario:status';
const KV_HISTORY = 'aquario:history';
const MAX_HIST   = 100; // últimas 100 leituras

// Token simples para autenticar o ESP32
// Configure em Vercel → Environment Variables → DEVICE_TOKEN
const DEVICE_TOKEN = process.env.DEVICE_TOKEN || 'mude-este-token';

module.exports = async function handler(req, res) {
  // CORS — permite o painel (frontend) chamar esta API
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');

  if (req.method === 'OPTIONS') {
    return res.status(200).end();
  }

  // ── GET: retorna último status + histórico ──────────────────────────────
  if (req.method === 'GET') {
    try {
      const [status, history] = await Promise.all([
        kv.get(KV_KEY),
        kv.lrange(KV_HISTORY, 0, MAX_HIST - 1),
      ]);
      return res.status(200).json({
        status:  status  || null,
        history: history || [],
      });
    } catch (e) {
      return res.status(500).json({ error: 'KV não disponível', detail: e.message });
    }
  }

  // ── POST: recebe dados do ESP32 ─────────────────────────────────────────
  if (req.method === 'POST') {
    // Autenticação simples por Bearer token
    const auth = req.headers.authorization || '';
    if (auth !== `Bearer ${DEVICE_TOKEN}`) {
      return res.status(401).json({ error: 'Token inválido' });
    }

    let body;
    try {
      body = typeof req.body === 'string' ? JSON.parse(req.body) : req.body;
    } catch {
      return res.status(400).json({ error: 'JSON inválido' });
    }

    // Adiciona timestamp do servidor se não vier do ESP32
    if (!body.server_ts) {
      body.server_ts = new Date().toISOString();
    }

    try {
      await Promise.all([
        // Salva status atual
        kv.set(KV_KEY, body, { ex: 300 }), // expira em 5min se ESP32 parar

        // Adiciona ao histórico (lista circular)
        kv.lpush(KV_HISTORY, JSON.stringify({
          temperatura: body.temperatura,
          nivel_cm:    body.nivel_cm,
          timestamp:   body.timestamp || body.server_ts,
        })),
        kv.ltrim(KV_HISTORY, 0, MAX_HIST - 1),
      ]);
      return res.status(200).json({ ok: true });
    } catch (e) {
      return res.status(500).json({ error: 'Falha ao salvar', detail: e.message });
    }
  }

  return res.status(405).json({ error: 'Método não permitido' });
};
