# Diagrama de Ligações - Aquário Inteligente

## ESP32 (30 pinos) → Componentes

### HC-SR04 (Ultrassônico - Nível de Água)
| HC-SR04 | ESP32      |
|---------|------------|
| VCC     | 5V (Vin)   |
| GND     | GND        |
| TRIG    | GPIO 5     |
| ECHO    | GPIO 18 (*)|

(*) IMPORTANTE: O HC-SR04 trabalha a 5V no ECHO.
    Use um divisor de tensão: ECHO → R1(1kΩ) → GPIO18 → R2(2kΩ) → GND

### DS18B20 (Temperatura)
| DS18B20 | ESP32    | Obs                      |
|---------|----------|--------------------------|
| VCC     | 3.3V     |                          |
| GND     | GND      |                          |
| DATA    | GPIO 19  | Resistor 4.7kΩ entre VCC e DATA |

### Servo Motor SG90 (Alimentador)
| Servo  | ESP32    |
|--------|----------|
| VCC (vermelho) | 5V (Vin) |
| GND (marrom)   | GND      |
| Sinal (laranja)| GPIO 21  |

⚠️ Se o servo causar instabilidade, alimente-o com fonte externa 5V
   (compartilhe apenas o GND com o ESP32)

### LCD 16x2 com módulo I2C
| LCD I2C | ESP32    |
|---------|----------|
| VCC     | 5V (Vin) |
| GND     | GND      |
| SDA     | GPIO 23  |
| SCL     | GPIO 25  |

### Relé para Bomba FP-220
| Módulo Relé | ESP32    | Obs                    |
|-------------|----------|------------------------|
| VCC         | 5V (Vin) |                        |
| GND         | GND      |                        |
| IN          | GPIO 22  |                        |
| COM         | Fase 110V|  ⚡ ATENÇÃO: ALTA TENSÃO |
| NA (Normal Aberto)| Fio da bomba|           |

⚡ SEGURANÇA: Use caixa plástica isolada para os terminais 110V/220V.
   Nunca deixe conexões de alta tensão expostas.

## Divisor de Tensão para o HC-SR04 ECHO

```
ECHO (5V) ─── R1 1kΩ ─── GPIO 18
                      |
                    R2 2kΩ
                      |
                     GND
```

## Alimentação Geral
- ESP32: USB ou regulador 5V/3A
- Servo + LCD: podem ser alimentados pelo Vin (5V) do ESP32 via USB
- Bomba FP-220: 110V/220V AC pelo módulo relé
- Sensor ultrassônico: 5V
- DS18B20: 3.3V

## Endereço I2C do LCD
Caso o LCD não funcione, verifique o endereço com o sketch I2C Scanner:
```cpp
#include <Wire.h>
void setup() {
  Wire.begin(23, 25);
  Serial.begin(115200);
}
void loop() {
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0)
      Serial.printf("Encontrado em: 0x%02X\n", a);
  }
  delay(5000);
}
```
Endereços comuns: 0x27 ou 0x3F
