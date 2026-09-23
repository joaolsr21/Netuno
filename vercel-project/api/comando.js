/**
 * /api/comando
 *
 * POST → recebe um comando do painel web e publica via MQTT no broker
 *
 * Body esperado:
 * {
 *   "topico": "aquario/alimentar" | "aquario/bomba" | "aquario/horario" | "aquario/config",
 *   "payload": { ...dados... }
 * }
 *
 * Variáveis de ambiente no Vercel (Settings → Environment Variables):
 *   MQTT_BROKER  → ex: mqtts://SEU_BROKER.s2.eu.hivemq.cloud:8883
 *   MQTT_USER    → usuário HiveMQ
 *   MQTT_PASS    → senha HiveMQ
 *   PANEL_TOKEN  → token que o painel envia para autenticar
 */

const mqtt = require('mqtt');

// Tópicos permitidos (whitelist de segurança)
const TOPICOS_PERMITIDOS = new Set([
  'aquario/alimentar',
  'aquario/bomba',
  'aquario/horario',
  'aquario/config',
  'aquario/cmd',
]);

const PANEL_TOKEN = process.env.PANEL_TOKEN || 'token-do-painel';

module.exports = async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');

  if (req.method === 'OPTIONS') return res.status(200).end();
  if (req.method !== 'POST')    return res.status(405).json({ error: 'Método não permitido' });

  // Autenticação do painel
  const auth = req.headers.authorization || '';
  if (auth !== `Bearer ${PANEL_TOKEN}`) {
    return res.status(401).json({ error: 'Não autorizado' });
  }

  const { topico, payload } = req.body || {};

  if (!topico || !TOPICOS_PERMITIDOS.has(topico)) {
    return res.status(400).json({ error: 'Tópico inválido ou não permitido' });
  }

  // Publicar via MQTT (conexão síncrona dentro da serverless function)
  return new Promise((resolve) => {
    const client = mqtt.connect(process.env.MQTT_BROKER, {
      username:  process.env.MQTT_USER,
      password:  process.env.MQTT_PASS,
      connectTimeout: 5000,
      reconnectPeriod: 0, // sem reconexão automática em serverless
    });

    const timeout = setTimeout(() => {
      client.end(true);
      res.status(504).json({ error: 'Timeout ao conectar ao broker MQTT' });
      resolve();
    }, 6000);

    client.on('connect', () => {
      client.publish(topico, JSON.stringify(payload), { qos: 1 }, (err) => {
        clearTimeout(timeout);
        client.end();
        if (err) {
          res.status(500).json({ error: 'Falha ao publicar', detail: err.message });
        } else {
          res.status(200).json({ ok: true, topico, payload });
        }
        resolve();
      });
    });

    client.on('error', (err) => {
      clearTimeout(timeout);
      client.end(true);
      res.status(500).json({ error: 'Erro MQTT', detail: err.message });
      resolve();
    });
  });
};
