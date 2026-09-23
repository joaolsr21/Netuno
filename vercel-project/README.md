# 🐠 Aquário Inteligente — Painel Vercel

Dashboard web em tempo real para monitorar e controlar o aquário via MQTT + API REST.

## Estrutura do Projeto

```
vercel-project/
├── public/
│   └── index.html       ← Painel web (frontend)
├── api/
│   ├── status.js        ← GET/POST dados do ESP32 (usa Vercel KV)
│   └── comando.js       ← Envia comandos para o ESP32 via MQTT
├── vercel.json          ← Configuração de rotas
├── package.json
└── README.md
```

## Pré-requisitos

1. Conta gratuita no [Vercel](https://vercel.com)
2. Conta gratuita no [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/)
3. Node.js 18+ instalado localmente (para testar)

---

## 1. Configurar o HiveMQ Cloud

1. Acesse [hivemq.com](https://www.hivemq.com/mqtt-cloud-broker/) e crie uma conta gratuita
2. Crie um novo cluster (Free tier — 100 conexões simultâneas)
3. Na aba **Access Management**, crie um usuário e senha
4. Anote:
   - **Host**: `xxx.s2.eu.hivemq.cloud`
   - **MQTT Port**: `8883` (TLS) — ESP32
   - **WebSocket Port**: `8884` (WSS) — Painel web

---

## 2. Deploy no Vercel

### Opção A — Via GitHub (recomendada)

```bash
# Clone / crie o repositório
git init
git add .
git commit -m "aquario inteligente"
git remote add origin https://github.com/SEU_USUARIO/aquario-inteligente.git
git push -u origin main
```

Depois no [vercel.com](https://vercel.com):
1. **New Project** → Importe o repositório
2. Framework Preset: **Other**
3. Root Directory: `vercel-project` (se estiver dentro de uma pasta)
4. Clique em **Deploy**

### Opção B — Via CLI

```bash
npm install -g vercel
cd vercel-project
vercel login
vercel --prod
```

---

## 3. Variáveis de Ambiente no Vercel

Vá em **Settings → Environment Variables** e adicione:

| Nome           | Valor                              | Descrição                         |
|----------------|------------------------------------|-----------------------------------|
| `MQTT_BROKER`  | `mqtts://xxx.hivemq.cloud:8883`    | URL do broker MQTT                |
| `MQTT_USER`    | `seu_usuario`                      | Usuário HiveMQ                    |
| `MQTT_PASS`    | `sua_senha`                        | Senha HiveMQ                      |
| `DEVICE_TOKEN` | `token-secreto-do-esp32`           | Token que o ESP32 usa no POST     |
| `PANEL_TOKEN`  | `token-secreto-do-painel`          | Token que o frontend usa          |

---

## 4. Vercel KV (banco de dados Redis)

Para o histórico de dados funcionar:

1. No dashboard Vercel → **Storage** → **Create Database** → **KV**
2. Nome: `aquario-kv`
3. Clique em **Connect to Project**
4. As variáveis `KV_URL`, `KV_REST_API_URL` e `KV_REST_API_TOKEN` são adicionadas automaticamente

Instale o pacote:
```bash
npm install @vercel/kv
```

---

## 5. Configurar o Frontend

No arquivo `public/index.html`, localize o objeto `CFG` e preencha:

```js
const CFG = {
  mqttUrl:    'wss://SEU_BROKER.s2.eu.hivemq.cloud:8884/mqtt',
  mqttUser:   'SEU_USUARIO',
  mqttPass:   'SUA_SENHA',
  panelToken: 'token-secreto-do-painel',  // igual ao PANEL_TOKEN
};
```

---

## 6. Configurar o ESP32

No arquivo `.ino`, preencha a seção de configurações:

```cpp
const char* WIFI_SSID     = "SuaRede";
const char* WIFI_PASSWORD = "SuaSenha";
const char* MQTT_HOST     = "SEU_BROKER.s2.eu.hivemq.cloud";
const char* MQTT_USER     = "SEU_USUARIO";
const char* MQTT_PASSWORD = "SUA_SENHA";
```

Opcional — POST HTTP para a API Vercel (além do MQTT):
```cpp
// Adicione esta função no .ino para enviar dados via HTTP
// POST https://seu-projeto.vercel.app/api/status
// Header: Authorization: Bearer TOKEN_DO_DISPOSITIVO
// Body: JSON com os dados do aquário
```

---

## Fluxo de Dados

```
ESP32 ──MQTT──► HiveMQ Cloud ◄──MQTT── Painel Vercel (browser)
  │                                            │
  └──HTTP POST──► /api/status ◄──HTTP GET──────┘
                  (Vercel KV)
```

- **MQTT**: tempo real, bidirecional, latência < 100ms
- **HTTP API**: fallback + persistência histórica no KV

---

## Endpoints da API

### `GET /api/status`
Retorna último estado + histórico de temperaturas.

```json
{
  "status": {
    "temperatura": 25.3,
    "nivel_cm": 4.2,
    "bomba": false,
    "alerta_nivel": false,
    "alerta_temp": false,
    "timestamp": "2026-09-23T14:30:00"
  },
  "history": [...]
}
```

### `POST /api/status`
Recebe dados do ESP32.
- Header: `Authorization: Bearer DEVICE_TOKEN`
- Body: mesmo JSON acima

### `POST /api/comando`
Envia comando ao ESP32 via MQTT.
- Header: `Authorization: Bearer PANEL_TOKEN`
- Body:
```json
{
  "topico": "aquario/alimentar",
  "payload": { "acao": "alimentar" }
}
```

---

## Tópicos MQTT

| Tópico              | Direção         | Descrição                     |
|---------------------|-----------------|-------------------------------|
| `aquario/status`    | ESP32 → Painel  | Dados em tempo real           |
| `aquario/alertas`   | ESP32 → Painel  | Alertas de nível/temperatura  |
| `aquario/alimentar` | Painel → ESP32  | Disparar alimentação          |
| `aquario/bomba`     | Painel → ESP32  | Ligar/desligar bomba          |
| `aquario/horario`   | Painel → ESP32  | Configurar horários           |
| `aquario/config`    | Painel → ESP32  | Configurar servo, limites     |
| `aquario/cmd`       | Painel → ESP32  | Comandos gerais (restart etc) |
